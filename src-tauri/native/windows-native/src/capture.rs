use std::sync::mpsc::{sync_channel, Receiver, SyncSender, TryRecvError};
use std::thread;
use std::time::Duration;

use windows::core::{IInspectable, Interface, HSTRING};
use windows::Foundation::TypedEventHandler;
use windows::Graphics::Capture::{
    Direct3D11CaptureFramePool, GraphicsCaptureItem, GraphicsCaptureSession,
};
use windows::Graphics::DirectX::DirectXPixelFormat;
use windows::Graphics::SizeInt32;
use windows::Win32::Graphics::Direct3D11::ID3D11Texture2D;
use windows::Win32::System::WinRT::Direct3D11::IDirect3DDxgiInterfaceAccess;
use windows::Win32::System::WinRT::Graphics::Capture::IGraphicsCaptureItemInterop;
use windows::Win32::System::WinRT::{
    RoGetActivationFactory, RoInitialize, RoUninitialize, RO_INIT_MULTITHREADED,
};

use crate::d3d11::D3d11Context;
use crate::encoder::NvencEncoder;
use crate::sources::monitor_for_source;
use crate::{CaptureHandle, EncodedPacket, NativeConfig, NativeError};

pub(crate) fn start(
    config: NativeConfig,
) -> Result<(CaptureHandle, Receiver<Result<EncodedPacket, NativeError>>), NativeError> {
    let (packet_tx, packet_rx) = sync_channel(128);
    let (stop_tx, stop_rx) = sync_channel(1);
    let (ready_tx, ready_rx) = std::sync::mpsc::channel();
    let worker_config = config.clone();
    let join = thread::Builder::new()
        .name("moonlit-wgc-nvenc".to_string())
        .spawn(move || run_worker(worker_config, stop_rx, packet_tx, ready_tx))
        .map_err(NativeError::from)?;

    match ready_rx.recv() {
        Ok(Ok(())) => {
            let handle = CaptureHandle::new(move || {
                let _ = stop_tx.send(());
                let _ = join.join();
            });
            Ok((handle, packet_rx))
        }
        Ok(Err(error)) => {
            let _ = join.join();
            Err(error)
        }
        Err(_) => {
            let _ = join.join();
            Err(NativeError::ChannelClosed)
        }
    }
}

fn run_worker(
    config: NativeConfig,
    stop_rx: Receiver<()>,
    packet_tx: SyncSender<Result<EncodedPacket, NativeError>>,
    ready_tx: std::sync::mpsc::Sender<Result<(), NativeError>>,
) {
    let result = run_worker_inner(config, stop_rx, packet_tx.clone(), &ready_tx);
    if let Err(error) = result {
        let _ = packet_tx.send(Err(error));
    }
}

fn run_worker_inner(
    config: NativeConfig,
    stop_rx: Receiver<()>,
    packet_tx: SyncSender<Result<EncodedPacket, NativeError>>,
    ready_tx: &std::sync::mpsc::Sender<Result<(), NativeError>>,
) -> Result<(), NativeError> {
    unsafe { RoInitialize(RO_INIT_MULTITHREADED) }
        .map_err(|error| NativeError::windows("RoInitialize", error))?;
    let mut pipeline = match CapturePipeline::new(config) {
        Ok(pipeline) => pipeline,
        Err(error) => {
            let _ = ready_tx.send(Err(error.clone()));
            unsafe { RoUninitialize() };
            return Err(error);
        }
    };
    ready_tx
        .send(Ok(()))
        .map_err(|_| NativeError::ChannelClosed)?;
    let result = pipeline.run(stop_rx, packet_tx);
    drop(pipeline);
    unsafe { RoUninitialize() };
    result
}

struct CapturePipeline {
    encoder: NvencEncoder,
    pool: Direct3D11CaptureFramePool,
    session: GraphicsCaptureSession,
    frame_token: i64,
    _frame_handler: TypedEventHandler<Direct3D11CaptureFramePool, IInspectable>,
    frame_rx: Receiver<()>,
    frame_duration_ms: u64,
    _context: D3d11Context,
}

impl CapturePipeline {
    fn new(config: NativeConfig) -> Result<Self, NativeError> {
        let target = monitor_for_source(&config.source_id)?;
        let context = D3d11Context::create()?;
        let item = create_monitor_item(target.handle)?;
        let size = item
            .Size()
            .map_err(|error| NativeError::windows("GraphicsCaptureItem::Size", error))?;
        let width = u32::try_from(size.Width)
            .map_err(|_| NativeError::Unsupported("capture width is invalid"))?;
        let height = u32::try_from(size.Height)
            .map_err(|_| NativeError::Unsupported("capture height is invalid"))?;
        if width != config.width || height != config.height {
            return Err(NativeError::Unsupported(
                "the first native spike does not resize WGC textures",
            ));
        }

        let pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            &context.winrt_device,
            DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            SizeInt32 {
                Width: size.Width,
                Height: size.Height,
            },
        )
        .map_err(|error| {
            NativeError::windows("Direct3D11CaptureFramePool::CreateFreeThreaded", error)
        })?;
        let session = pool.CreateCaptureSession(&item).map_err(|error| {
            NativeError::windows("Direct3D11CaptureFramePool::CreateCaptureSession", error)
        })?;
        session.SetIsCursorCaptureEnabled(true).map_err(|error| {
            NativeError::windows("GraphicsCaptureSession::SetIsCursorCaptureEnabled", error)
        })?;

        let (frame_tx, frame_rx) = sync_channel(8);
        let frame_handler: TypedEventHandler<Direct3D11CaptureFramePool, IInspectable> =
            TypedEventHandler::new(move |_pool, _args| {
                let _ = frame_tx.try_send(());
                Ok(())
            });
        let frame_token = pool.FrameArrived(&frame_handler).map_err(|error| {
            NativeError::windows("Direct3D11CaptureFramePool::FrameArrived", error)
        })?;
        session
            .StartCapture()
            .map_err(|error| NativeError::windows("GraphicsCaptureSession::StartCapture", error))?;

        let encoder = NvencEncoder::new(&context, width, height, config.fps)?;
        Ok(Self {
            encoder,
            pool,
            session,
            frame_token,
            _frame_handler: frame_handler,
            frame_rx,
            frame_duration_ms: (1000 / config.fps.max(1) as u64).max(1),
            _context: context,
        })
    }

    fn run(
        &mut self,
        stop_rx: Receiver<()>,
        packet_tx: SyncSender<Result<EncodedPacket, NativeError>>,
    ) -> Result<(), NativeError> {
        let mut first_timestamp = None;
        loop {
            match stop_rx.recv_timeout(Duration::from_millis(100)) {
                Ok(()) => break,
                Err(std::sync::mpsc::RecvTimeoutError::Disconnected) => break,
                Err(std::sync::mpsc::RecvTimeoutError::Timeout) => {}
            }

            loop {
                match self.frame_rx.try_recv() {
                    Ok(()) => {}
                    Err(TryRecvError::Empty) => break,
                    Err(TryRecvError::Disconnected) => return Err(NativeError::ChannelClosed),
                }
                self.drain_frames(&mut first_timestamp, &packet_tx)?;
            }
        }

        self.encoder.finish()?;
        self.pool
            .RemoveFrameArrived(self.frame_token)
            .map_err(|error| {
                NativeError::windows("Direct3D11CaptureFramePool::RemoveFrameArrived", error)
            })?;
        self.session
            .Close()
            .map_err(|error| NativeError::windows("GraphicsCaptureSession::Close", error))?;
        self.pool
            .Close()
            .map_err(|error| NativeError::windows("Direct3D11CaptureFramePool::Close", error))?;
        Ok(())
    }

    fn drain_frames(
        &mut self,
        first_timestamp: &mut Option<i64>,
        packet_tx: &SyncSender<Result<EncodedPacket, NativeError>>,
    ) -> Result<(), NativeError> {
        loop {
            let frame = match self.pool.TryGetNextFrame() {
                Ok(frame) => frame,
                Err(_) => break,
            };
            let timestamp = frame
                .SystemRelativeTime()
                .map_err(|error| {
                    NativeError::windows("Direct3D11CaptureFrame::SystemRelativeTime", error)
                })?
                .Duration;
            let base = *first_timestamp.get_or_insert(timestamp);
            let pts_ms = timestamp.saturating_sub(base).max(0).unsigned_abs() / 10_000;
            let surface = frame
                .Surface()
                .map_err(|error| NativeError::windows("Direct3D11CaptureFrame::Surface", error))?;
            let access: IDirect3DDxgiInterfaceAccess = surface.cast().map_err(|error| {
                NativeError::windows(
                    "IDirect3DSurface::cast<IDirect3DDxgiInterfaceAccess>",
                    error,
                )
            })?;
            let texture: ID3D11Texture2D = unsafe { access.GetInterface() }.map_err(|error| {
                NativeError::windows("IDirect3DDxgiInterfaceAccess::GetInterface", error)
            })?;
            if let Some(mut packet) =
                self.encoder
                    .encode_texture(&self._context, &texture, pts_ms)?
            {
                packet.duration_ms = self.frame_duration_ms;
                packet_tx
                    .send(Ok(packet))
                    .map_err(|_| NativeError::ChannelClosed)?;
            }
            frame
                .Close()
                .map_err(|error| NativeError::windows("Direct3D11CaptureFrame::Close", error))?;
        }
        Ok(())
    }
}

fn create_monitor_item(
    monitor: windows::Win32::Graphics::Gdi::HMONITOR,
) -> Result<GraphicsCaptureItem, NativeError> {
    let class_id = HSTRING::from("Windows.Graphics.Capture.GraphicsCaptureItem");
    let interop: IGraphicsCaptureItemInterop = unsafe { RoGetActivationFactory(&class_id) }
        .map_err(|error| {
            NativeError::windows("RoGetActivationFactory(GraphicsCaptureItem)", error)
        })?;
    unsafe { interop.CreateForMonitor(monitor) }.map_err(|error| {
        NativeError::windows("IGraphicsCaptureItemInterop::CreateForMonitor", error)
    })
}
