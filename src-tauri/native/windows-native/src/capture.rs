use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{sync_channel, Receiver, SyncSender, TrySendError};
use std::sync::Arc;
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

const CONTROL_CHANNEL_CAPACITY: usize = 32;

#[derive(Clone, Copy, Debug)]
enum CaptureEvent {
    FrameArrived,
}

pub(crate) fn start(
    config: NativeConfig,
) -> Result<(CaptureHandle, Receiver<Result<EncodedPacket, NativeError>>), NativeError> {
    let (packet_tx, packet_rx) = sync_channel(128);
    let (event_tx, event_rx) = sync_channel(CONTROL_CHANNEL_CAPACITY);
    let (ready_tx, ready_rx) = std::sync::mpsc::channel();
    let stop_flag = Arc::new(AtomicBool::new(false));
    let source_closed = Arc::new(AtomicBool::new(false));
    let worker_stop_flag = Arc::clone(&stop_flag);
    let worker_source_closed = Arc::clone(&source_closed);
    let worker_event_tx = event_tx.clone();
    let worker_config = config.clone();
    let join = thread::Builder::new()
        .name("moonlit-wgc-nvenc".to_string())
        .spawn(move || {
            run_worker(
                worker_config,
                worker_event_tx,
                event_rx,
                packet_tx,
                ready_tx,
                worker_stop_flag,
                worker_source_closed,
            )
        })
        .map_err(NativeError::from)?;

    match ready_rx.recv() {
        Ok(Ok(())) => {
            let handle_stop_flag = Arc::clone(&stop_flag);
            let handle = CaptureHandle::new(move || {
                handle_stop_flag.store(true, Ordering::Release);
                match join.join() {
                    Ok(result) => result,
                    Err(_) => Err(NativeError::WorkerPanicked),
                }
            });
            Ok((handle, packet_rx))
        }
        Ok(Err(error)) => {
            stop_flag.store(true, Ordering::Release);
            let _ = join.join();
            Err(error)
        }
        Err(_) => {
            stop_flag.store(true, Ordering::Release);
            let _ = join.join();
            Err(NativeError::ChannelClosed)
        }
    }
}

fn run_worker(
    config: NativeConfig,
    event_tx: SyncSender<CaptureEvent>,
    event_rx: Receiver<CaptureEvent>,
    packet_tx: SyncSender<Result<EncodedPacket, NativeError>>,
    ready_tx: std::sync::mpsc::Sender<Result<(), NativeError>>,
    stop_flag: Arc<AtomicBool>,
    source_closed: Arc<AtomicBool>,
) -> Result<(), NativeError> {
    let result = run_worker_inner(
        config,
        event_tx,
        event_rx,
        packet_tx.clone(),
        &ready_tx,
        stop_flag,
        source_closed,
    );
    if let Err(error) = &result {
        let _ = packet_tx.try_send(Err(error.clone()));
    }
    result
}

fn run_worker_inner(
    config: NativeConfig,
    event_tx: SyncSender<CaptureEvent>,
    event_rx: Receiver<CaptureEvent>,
    packet_tx: SyncSender<Result<EncodedPacket, NativeError>>,
    ready_tx: &std::sync::mpsc::Sender<Result<(), NativeError>>,
    stop_flag: Arc<AtomicBool>,
    source_closed: Arc<AtomicBool>,
) -> Result<(), NativeError> {
    if let Err(error) = unsafe { RoInitialize(RO_INIT_MULTITHREADED) }
        .map_err(|error| NativeError::windows("RoInitialize", error))
    {
        let _ = ready_tx.send(Err(error.clone()));
        return Err(error);
    }

    let mut pipeline = match CapturePipeline::new(config, event_tx, source_closed) {
        Ok(pipeline) => pipeline,
        Err(error) => {
            let _ = ready_tx.send(Err(error.clone()));
            unsafe { RoUninitialize() };
            return Err(error);
        }
    };

    if ready_tx.send(Ok(())).is_err() {
        let _ = pipeline.encoder.finish();
        let _ = pipeline.shutdown();
        unsafe { RoUninitialize() };
        return Err(NativeError::ChannelClosed);
    }
    let result = pipeline.run(event_rx, &stop_flag, packet_tx);
    drop(pipeline);
    unsafe { RoUninitialize() };
    result
}

struct CapturePipeline {
    encoder: NvencEncoder,
    pool: Direct3D11CaptureFramePool,
    session: GraphicsCaptureSession,
    item: GraphicsCaptureItem,
    frame_token: i64,
    closed_token: i64,
    _frame_handler: TypedEventHandler<Direct3D11CaptureFramePool, IInspectable>,
    _closed_handler: TypedEventHandler<GraphicsCaptureItem, IInspectable>,
    _context: D3d11Context,
    fps: u32,
    source_closed: Arc<AtomicBool>,
}

impl CapturePipeline {
    fn new(
        config: NativeConfig,
        event_tx: SyncSender<CaptureEvent>,
        source_closed: Arc<AtomicBool>,
    ) -> Result<Self, NativeError> {
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
                "the native spike requires the requested size to match the source",
            ));
        }

        let encoder = NvencEncoder::new(&context, width, height, config.fps)?;
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

        let frame_tx = event_tx.clone();
        let frame_handler: TypedEventHandler<Direct3D11CaptureFramePool, IInspectable> =
            TypedEventHandler::new(move |_pool, _args| {
                let _ = frame_tx.try_send(CaptureEvent::FrameArrived);
                Ok(())
            });
        let frame_token = pool.FrameArrived(&frame_handler).map_err(|error| {
            NativeError::windows("Direct3D11CaptureFramePool::FrameArrived", error)
        })?;

        let closed_source = Arc::clone(&source_closed);
        let closed_handler: TypedEventHandler<GraphicsCaptureItem, IInspectable> =
            TypedEventHandler::new(move |_item, _args| {
                closed_source.store(true, Ordering::Release);
                Ok(())
            });
        let closed_token = item
            .Closed(&closed_handler)
            .map_err(|error| NativeError::windows("GraphicsCaptureItem::Closed", error))?;

        session
            .StartCapture()
            .map_err(|error| NativeError::windows("GraphicsCaptureSession::StartCapture", error))?;

        Ok(Self {
            encoder,
            pool,
            session,
            item,
            frame_token,
            closed_token,
            _frame_handler: frame_handler,
            _closed_handler: closed_handler,
            _context: context,
            fps: config.fps,
            source_closed,
        })
    }

    fn run(
        &mut self,
        event_rx: Receiver<CaptureEvent>,
        stop_flag: &AtomicBool,
        packet_tx: SyncSender<Result<EncodedPacket, NativeError>>,
    ) -> Result<(), NativeError> {
        let mut first_timestamp_100ns = None;
        let mut pending_packet = None;
        let capture_result = loop {
            if stop_flag.load(Ordering::Acquire) {
                break Ok(());
            }
            if self.source_closed.load(Ordering::Acquire) {
                break Err(NativeError::SourceEnded);
            }
            match event_rx.recv_timeout(Duration::from_millis(100)) {
                Ok(CaptureEvent::FrameArrived) => {
                    if let Err(error) = self.drain_frames(
                        stop_flag,
                        &mut first_timestamp_100ns,
                        &mut pending_packet,
                        &packet_tx,
                    ) {
                        break Err(error);
                    }
                }
                Err(std::sync::mpsc::RecvTimeoutError::Timeout) => {}
                Err(std::sync::mpsc::RecvTimeoutError::Disconnected) => {
                    break Err(NativeError::ChannelClosed)
                }
            }
        };

        let pending_result = flush_pending(stop_flag, &packet_tx, &mut pending_packet, self.fps);
        let finish_result = self.encoder.finish();
        let cleanup_result = self.shutdown();
        first_error(
            capture_result,
            pending_result,
            finish_result,
            cleanup_result,
        )
    }

    fn shutdown(&mut self) -> Result<(), NativeError> {
        let mut first = None;
        record_first(
            &mut first,
            self.pool
                .RemoveFrameArrived(self.frame_token)
                .map_err(|error| {
                    NativeError::windows("Direct3D11CaptureFramePool::RemoveFrameArrived", error)
                }),
        );
        record_first(
            &mut first,
            self.item
                .RemoveClosed(self.closed_token)
                .map_err(|error| NativeError::windows("GraphicsCaptureItem::RemoveClosed", error)),
        );
        record_first(
            &mut first,
            self.session
                .Close()
                .map_err(|error| NativeError::windows("GraphicsCaptureSession::Close", error)),
        );
        record_first(
            &mut first,
            self.pool
                .Close()
                .map_err(|error| NativeError::windows("Direct3D11CaptureFramePool::Close", error)),
        );
        first.map_or(Ok(()), Err)
    }

    fn drain_frames(
        &mut self,
        stop_flag: &AtomicBool,
        first_timestamp_100ns: &mut Option<u64>,
        pending_packet: &mut Option<EncodedPacket>,
        packet_tx: &SyncSender<Result<EncodedPacket, NativeError>>,
    ) -> Result<(), NativeError> {
        loop {
            if stop_flag.load(Ordering::Acquire) {
                return Ok(());
            }
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
            let timestamp_100ns = u64::try_from(timestamp)
                .map_err(|_| NativeError::Unsupported("capture timestamp is invalid"))?;
            let base = *first_timestamp_100ns.get_or_insert(timestamp_100ns);
            let pts_100ns = timestamp_100ns.saturating_sub(base);

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
            let packet = self
                .encoder
                .encode_texture(&self._context, &texture, pts_100ns)?;
            drop(texture);
            drop(access);
            drop(surface);
            frame
                .Close()
                .map_err(|error| NativeError::windows("Direct3D11CaptureFrame::Close", error))?;

            if let Some(packet) = packet {
                if let Some(mut previous) = pending_packet.take() {
                    previous.duration_100ns = pts_100ns
                        .saturating_sub(previous.pts_100ns)
                        .max(frame_duration(self.fps));
                    send_packet(stop_flag, packet_tx, previous)?;
                }
                *pending_packet = Some(packet);
            }
        }
        Ok(())
    }
}

fn send_packet(
    stop_flag: &AtomicBool,
    packet_tx: &SyncSender<Result<EncodedPacket, NativeError>>,
    packet: EncodedPacket,
) -> Result<(), NativeError> {
    let mut pending = Ok(packet);
    loop {
        if stop_flag.load(Ordering::Acquire) {
            return Ok(());
        }
        match packet_tx.try_send(pending) {
            Ok(()) => return Ok(()),
            Err(TrySendError::Full(item)) => {
                pending = item;
                thread::sleep(Duration::from_millis(1));
            }
            Err(TrySendError::Disconnected(_)) => return Err(NativeError::ChannelClosed),
        }
    }
}

fn first_error(
    first: Result<(), NativeError>,
    second: Result<(), NativeError>,
    third: Result<(), NativeError>,
    fourth: Result<(), NativeError>,
) -> Result<(), NativeError> {
    let mut error = first.err();
    if let Err(next) = second {
        error.get_or_insert(next);
    }
    if let Err(next) = third {
        error.get_or_insert(next);
    }
    if let Err(next) = fourth {
        error.get_or_insert(next);
    }
    error.map_or(Ok(()), Err)
}

fn flush_pending(
    stop_flag: &AtomicBool,
    packet_tx: &SyncSender<Result<EncodedPacket, NativeError>>,
    pending_packet: &mut Option<EncodedPacket>,
    fps: u32,
) -> Result<(), NativeError> {
    if let Some(mut packet) = pending_packet.take() {
        packet.duration_100ns = frame_duration(fps);
        send_packet(stop_flag, packet_tx, packet)?;
    }
    Ok(())
}

fn frame_duration(fps: u32) -> u64 {
    10_000_000 / u64::from(fps.max(1))
}

fn record_first(first: &mut Option<NativeError>, result: Result<(), NativeError>) {
    if first.is_none() {
        if let Err(error) = result {
            *first = Some(error);
        }
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
