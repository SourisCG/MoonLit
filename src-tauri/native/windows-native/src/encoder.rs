use windows::Win32::Graphics::Direct3D11::ID3D11Texture2D;

use nvenc::session::{InitParams, NeedsConfig, Session};
use nvenc::sys::enums::{NVencBufferFormat, NVencPicStruct, NVencPicType, NVencTuningInfo};
use nvenc::sys::guids::{NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_P4_GUID};

use crate::d3d11::D3d11Context;
use crate::{EncodedPacket, NativeError};

pub(crate) struct NvencEncoder {
    input_resource: nvenc::encoder::RegisteredResource,
    bitstream: nvenc::bitstream::BitStream,
    encoder: nvenc::encoder::Encoder,
    input_texture: ID3D11Texture2D,
    width: u32,
    height: u32,
    fps: u32,
    frame_index: usize,
}

impl NvencEncoder {
    pub(crate) fn probe(context: &D3d11Context) -> Result<(), NativeError> {
        ensure_library()?;
        let session = open_session(&context.device)?;
        let codecs = session.get_encode_codecs().map_err(|error| {
            NativeError::EncoderUnavailable(format!("NVENC codec probe: {error:?}"))
        })?;
        if codecs.contains(&NV_ENC_CODEC_H264_GUID) {
            Ok(())
        } else {
            Err(NativeError::EncoderUnavailable(
                "NVENC H.264 is not exposed by this driver".to_string(),
            ))
        }
    }

    pub(crate) fn new(
        context: &D3d11Context,
        width: u32,
        height: u32,
        fps: u32,
    ) -> Result<Self, NativeError> {
        if width == 0 || height == 0 || fps == 0 {
            return Err(NativeError::InvalidConfig(
                "NVENC dimensions and FPS must be greater than zero",
            ));
        }
        ensure_library()?;
        let session = open_session(&context.device)?;
        let codecs = session.get_encode_codecs().map_err(|error| {
            NativeError::EncoderUnavailable(format!("NVENC codec probe: {error:?}"))
        })?;
        if !codecs.contains(&NV_ENC_CODEC_H264_GUID) {
            return Err(NativeError::EncoderUnavailable(
                "NVENC H.264 is not exposed by this driver".to_string(),
            ));
        }

        let (session, mut preset) = session
            .get_encode_preset_config_ex(
                NV_ENC_CODEC_H264_GUID,
                NV_ENC_PRESET_P4_GUID,
                NVencTuningInfo::LowLatency,
            )
            .map_err(|error| {
                NativeError::EncoderUnavailable(format!("NVENC preset configuration: {error:?}"))
            })?;
        preset.preset_cfg.gop_len = fps.saturating_mul(2);
        preset.preset_cfg.frame_interval_p = 1;

        let encoder = session
            .init_encoder(InitParams {
                encode_guid: NV_ENC_CODEC_H264_GUID,
                preset_guid: NV_ENC_PRESET_P4_GUID,
                resolution: [width, height],
                aspect_ratio: [width, height],
                frame_rate: [fps, 1],
                tuning_info: NVencTuningInfo::LowLatency,
                buffer_format: NVencBufferFormat::ARGB,
                encode_config: &mut preset.preset_cfg,
                enable_ptd: true,
                max_encoder_resolution: [width, height],
            })
            .map_err(|error| {
                NativeError::EncoderUnavailable(format!("NVENC encoder initialization: {error:?}"))
            })?;
        let input_texture = context.create_encoder_texture(width, height)?;
        let input_resource = encoder
            .register_resource_dx11(
                &input_texture,
                NVencBufferFormat::ARGB,
                width.saturating_mul(4),
            )
            .map_err(|error| {
                NativeError::EncoderUnavailable(format!(
                    "NVENC register encoder texture: {error:?}"
                ))
            })?;
        let bitstream = encoder.create_bitstream_buffer().map_err(|error| {
            NativeError::EncoderUnavailable(format!("NVENC bitstream buffer: {error:?}"))
        })?;
        Ok(Self {
            input_resource,
            input_texture,
            encoder,
            bitstream,
            width,
            height,
            fps,
            frame_index: 0,
        })
    }

    pub(crate) fn encode_texture(
        &mut self,
        context: &D3d11Context,
        texture: &ID3D11Texture2D,
        pts_ms: u64,
    ) -> Result<Option<EncodedPacket>, NativeError> {
        context.copy_resource(&self.input_texture, texture)?;
        let picture_type = if self.frame_index == 0 {
            NVencPicType::IDR
        } else {
            NVencPicType::P
        };
        self.encoder
            .encode_picture(
                &self.input_resource,
                &self.bitstream,
                self.frame_index,
                pts_ms,
                NVencBufferFormat::ARGB,
                NVencPicStruct::Frame,
                picture_type,
                None,
            )
            .map_err(|error| {
                NativeError::EncoderUnavailable(format!("NVENC encode picture: {error:?}"))
            })?;
        self.frame_index = self.frame_index.saturating_add(1);

        let lock = self.bitstream.try_lock(true).map_err(|error| {
            NativeError::EncoderUnavailable(format!("NVENC lock bitstream: {error:?}"))
        })?;
        if lock.as_slice().is_empty() {
            return Ok(None);
        }

        let data = lock.as_slice().to_vec();
        let is_keyframe = contains_idr_nal(&data);
        Ok(Some(EncodedPacket {
            pts_ms,
            duration_ms: 1000 / self.fps.max(1) as u64,
            is_keyframe,
            data,
        }))
    }

    pub(crate) fn finish(&mut self) -> Result<(), NativeError> {
        // The first spike uses synchronous, no-B-frame output. Every submitted
        // frame is already locked before shutdown, so an EOS submission is not
        // needed and triggers a driver fault with this SDK wrapper.
        Ok(())
    }

    #[allow(dead_code)]
    pub(crate) fn dimensions(&self) -> (u32, u32) {
        (self.width, self.height)
    }
}

fn ensure_library() -> Result<(), NativeError> {
    nvenc::nvenc_init().map(|_| ()).map_err(|error| {
        NativeError::EncoderUnavailable(format!("nvEncodeAPI64.dll could not be loaded: {error}"))
    })
}

fn open_session(
    device: &impl windows::core::Interface,
) -> Result<Session<NeedsConfig>, NativeError> {
    Session::open_dx(device)
        .map_err(|error| NativeError::EncoderUnavailable(format!("NVENC D3D11 session: {error:?}")))
}

fn contains_idr_nal(data: &[u8]) -> bool {
    let mut index = 0;
    while index + 4 <= data.len() {
        let (start, next) = if data[index..].starts_with(&[0, 0, 0, 1]) {
            (index + 4, index + 4)
        } else if data[index..].starts_with(&[0, 0, 1]) {
            (index + 3, index + 3)
        } else {
            index += 1;
            continue;
        };
        if start < data.len() && data[start] & 0x1f == 5 {
            return true;
        }
        index = next;
    }
    false
}

#[cfg(test)]
mod tests {
    use super::contains_idr_nal;

    #[test]
    fn detects_annex_b_idr_nal() {
        assert!(contains_idr_nal(&[0, 0, 0, 1, 0x65, 1, 2]));
        assert!(contains_idr_nal(&[0, 0, 1, 0x65]));
        assert!(!contains_idr_nal(&[0, 0, 0, 1, 0x41]));
    }
}
