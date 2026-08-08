#include "MoonLitTest.hpp"

#include <moonlit/output/EncoderResolver.hpp>

#include <set>

using namespace MoonLit;
using namespace MoonLitTest;

MOONLIT_TEST(resolver_maps_all_simple_tokens)
{
	bool ok = expect(EncoderResolver::SimpleTokenToEncoderId("nvenc") == "obs_nvenc_h264_tex",
			 "nvenc token maps to the h264 nvenc encoder", failure);
	ok &= expect(EncoderResolver::SimpleTokenToEncoderId("nvenc_hevc") == "obs_nvenc_hevc_tex",
		     "nvenc_hevc token maps to the hevc nvenc encoder", failure);
	ok &= expect(EncoderResolver::SimpleTokenToEncoderId("nvenc_av1") == "obs_nvenc_av1_tex",
		     "nvenc_av1 token maps to the av1 nvenc encoder", failure);
	ok &= expect(EncoderResolver::SimpleTokenToEncoderId("qsv") == "obs_qsv11_v2",
		     "qsv token maps to the qsv encoder", failure);
	ok &= expect(EncoderResolver::SimpleTokenToEncoderId("qsv_av1") == "obs_qsv11_av1",
		     "qsv_av1 token maps to the qsv av1 encoder", failure);
	ok &= expect(EncoderResolver::SimpleTokenToEncoderId("amd") == "h264_texture_amf",
		     "amd token maps to the amf encoder", failure);
	ok &= expect(EncoderResolver::SimpleTokenToEncoderId("amd_hevc") == "h265_texture_amf",
		     "amd_hevc token maps to the amf hevc encoder", failure);
	ok &= expect(EncoderResolver::SimpleTokenToEncoderId("amd_av1") == "av1_texture_amf",
		     "amd_av1 token maps to the amf av1 encoder", failure);
	ok &= expect(EncoderResolver::SimpleTokenToEncoderId("x264") == "obs_x264", "x264 token maps", failure);
	ok &= expect(EncoderResolver::SimpleTokenToEncoderId("not_a_token").empty(),
		     "unknown tokens map to nothing", failure);
	return ok;
}

MOONLIT_TEST(resolver_uses_available_encoders)
{
	EncoderResolver resolver([](const char *id) {
		return std::string(id) == "obs_nvenc_h264_tex" || std::string(id) == "obs_x264";
	});

	const EncoderResolution resolved = resolver.Resolve("nvenc");
	bool ok = expect(resolved.effectiveId == "obs_nvenc_h264_tex", "available nvenc is used", failure);
	ok &= expect(!resolved.changed, "no change when the request is available", failure);
	return ok;
}

MOONLIT_TEST(resolver_falls_back_with_reason)
{
	EncoderResolver resolver([](const char *id) { return std::string(id) == "obs_x264"; });

	const EncoderResolution resolved = resolver.Resolve("nvenc_hevc");
	bool ok = expect(resolved.changed, "unavailable encoder is flagged as changed", failure);
	ok &= expect(resolved.effectiveId == "obs_x264", "fallback chain is used", failure);
	ok &= expect(!resolved.reason.empty(), "a visible reason is produced", failure);
	return ok;
}

MOONLIT_TEST(resolver_handles_raw_obs_ids)
{
	/* ffmpeg svt/aom ids have no simple token; the resolver accepts them as
	 * raw obs ids when registered and falls back otherwise. */
	EncoderResolver withSvt([](const char *id) {
		return std::string(id) == "ffmpeg_svt_av1" || std::string(id) == "obs_x264";
	});
	const EncoderResolution svt = withSvt.Resolve("ffmpeg_svt_av1");
	bool ok = expect(svt.effectiveId == "ffmpeg_svt_av1" && !svt.changed,
			 "registered raw obs id is used directly", failure);

	EncoderResolver withoutSvt([](const char *id) { return std::string(id) == "obs_x264"; });
	const EncoderResolution fallen = withoutSvt.Resolve("ffmpeg_svt_av1");
	ok &= expect(fallen.changed && fallen.effectiveId == "obs_x264",
		     "unregistered raw obs id falls back to the chain", failure);
	return ok;
}

MOONLIT_TEST(resolver_maps_preset_keys)
{
	bool ok = expect(std::string(EncoderResolver::SimpleTokenToPresetKey("qsv")) == "QSVPreset",
			 "qsv preset lives under QSVPreset", failure);
	ok &= expect(std::string(EncoderResolver::SimpleTokenToPresetKey("qsv_av1")) == "QSVPreset",
		     "qsv_av1 preset lives under QSVPreset", failure);
	ok &= expect(std::string(EncoderResolver::SimpleTokenToPresetKey("amd")) == "AMDPreset",
		     "amd preset lives under AMDPreset", failure);
	ok &= expect(std::string(EncoderResolver::SimpleTokenToPresetKey("amd_hevc")) == "AMDPreset",
		     "amd_hevc preset lives under AMDPreset", failure);
	ok &= expect(std::string(EncoderResolver::SimpleTokenToPresetKey("amd_av1")) == "AMDAV1Preset",
		     "amd_av1 preset lives under AMDAV1Preset", failure);
	ok &= expect(std::string(EncoderResolver::SimpleTokenToPresetKey("nvenc")) == "NVENCPreset2",
		     "nvenc preset lives under NVENCPreset2", failure);
	ok &= expect(std::string(EncoderResolver::SimpleTokenToPresetKey("nvenc_hevc")) == "NVENCPreset2",
		     "nvenc_hevc preset lives under NVENCPreset2", failure);
	ok &= expect(std::string(EncoderResolver::SimpleTokenToPresetKey("nvenc_av1")) == "NVENCPreset2",
		     "nvenc_av1 preset lives under NVENCPreset2", failure);
	ok &= expect(std::string(EncoderResolver::SimpleTokenToPresetKey("x264")) == "Preset",
		     "x264 preset lives under Preset", failure);
	ok &= expect(std::string(EncoderResolver::SimpleTokenToPresetKey("ffmpeg_svt_av1")) == "Preset",
		     "raw obs ids use the generic Preset key", failure);
	ok &= expect(std::string(EncoderResolver::SimpleTokenToPresetKey("")) == "Preset",
		     "empty token falls back to the generic Preset key", failure);
	return ok;
}

MOONLIT_TEST(resolver_first_available_follows_chain_order)
{
	EncoderResolver resolver([](const char *) { return true; });
	bool ok = expect(resolver.FirstAvailable() == "obs_nvenc_h264_tex",
			 "nvenc is first when everything is available", failure);

	EncoderResolver onlyX264([](const char *id) { return std::string(id) == "obs_x264"; });
	ok &= expect(onlyX264.FirstAvailable() == "obs_x264", "last chain entry is used when alone", failure);

	EncoderResolver none([](const char *) { return false; });
	ok &= expect(none.FirstAvailable().empty(), "no encoder yields an empty result", failure);
	return ok;
}
