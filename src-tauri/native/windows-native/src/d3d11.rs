use windows::core::Interface;
use windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
use windows::Win32::Foundation::HMODULE;
use windows::Win32::Graphics::Direct3D::{
    D3D_DRIVER_TYPE_HARDWARE, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1,
};
use windows::Win32::Graphics::Direct3D11::{
    D3D11CreateDevice, ID3D11Device, ID3D11DeviceContext, ID3D11Texture2D,
    D3D11_CREATE_DEVICE_BGRA_SUPPORT, D3D11_SDK_VERSION, D3D11_TEXTURE2D_DESC, D3D11_USAGE_DEFAULT,
};
use windows::Win32::Graphics::Dxgi::Common::{DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_SAMPLE_DESC};
use windows::Win32::Graphics::Dxgi::IDXGIDevice;
use windows::Win32::System::WinRT::Direct3D11::CreateDirect3D11DeviceFromDXGIDevice;

use crate::NativeError;

pub(crate) struct D3d11Context {
    pub device: ID3D11Device,
    pub _context: ID3D11DeviceContext,
    pub winrt_device: IDirect3DDevice,
}

impl D3d11Context {
    pub(crate) fn create() -> Result<Self, NativeError> {
        let mut device = None;
        let mut context = None;
        let mut feature_level = D3D_FEATURE_LEVEL_11_0;
        let feature_levels = [D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0];

        unsafe {
            D3D11CreateDevice(
                None,
                D3D_DRIVER_TYPE_HARDWARE,
                HMODULE::default(),
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                Some(&feature_levels),
                D3D11_SDK_VERSION,
                Some(&mut device),
                Some(&mut feature_level),
                Some(&mut context),
            )
        }
        .map_err(|error| NativeError::windows("D3D11CreateDevice", error))?;

        let device = device.ok_or(NativeError::Unsupported("D3D11 returned no device"))?;
        let context = context.ok_or(NativeError::Unsupported("D3D11 returned no context"))?;
        let dxgi_device: IDXGIDevice = device
            .cast()
            .map_err(|error| NativeError::windows("ID3D11Device::cast<IDXGIDevice>", error))?;
        let inspectable = unsafe { CreateDirect3D11DeviceFromDXGIDevice(&dxgi_device) }
            .map_err(|error| NativeError::windows("CreateDirect3D11DeviceFromDXGIDevice", error))?;
        let winrt_device = inspectable
            .cast()
            .map_err(|error| NativeError::windows("IInspectable::cast<IDirect3DDevice>", error))?;

        Ok(Self {
            device,
            _context: context,
            winrt_device,
        })
    }

    pub(crate) fn create_encoder_texture(
        &self,
        width: u32,
        height: u32,
    ) -> Result<ID3D11Texture2D, NativeError> {
        let description = D3D11_TEXTURE2D_DESC {
            Width: width,
            Height: height,
            MipLevels: 1,
            ArraySize: 1,
            Format: DXGI_FORMAT_B8G8R8A8_UNORM,
            SampleDesc: DXGI_SAMPLE_DESC {
                Count: 1,
                Quality: 0,
            },
            Usage: D3D11_USAGE_DEFAULT,
            BindFlags: 0,
            CPUAccessFlags: 0,
            MiscFlags: 0,
        };
        let mut texture = None;
        unsafe {
            self.device
                .CreateTexture2D(&description, None, Some(&mut texture))
        }
        .map_err(|error| NativeError::windows("ID3D11Device::CreateTexture2D", error))?;
        texture.ok_or(NativeError::Unsupported(
            "D3D11 returned no encoder texture",
        ))
    }

    pub(crate) fn copy_resource(
        &self,
        destination: &ID3D11Texture2D,
        source: &ID3D11Texture2D,
    ) -> Result<(), NativeError> {
        unsafe { self._context.CopyResource(destination, source) };
        unsafe { self._context.Flush() };
        Ok(())
    }
}
