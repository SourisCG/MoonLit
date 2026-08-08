#include "EncoderResolver.hpp"

#include <algorithm>

using namespace std;

namespace MoonLit {

static const vector<string> &Chain()
{
	static const vector<string> chain = {
		"obs_nvenc_h264_tex",
		"ffmpeg_nvenc",
		"obs_qsv11_v2",
		"obs_qsv11",
		"h264_texture_amf",
		"obs_x264",
	};
	return chain;
}

EncoderResolver::EncoderResolver(AvailableFn available) : available_(move(available)) {}

const vector<string> &EncoderResolver::FallbackChain()
{
	return Chain();
}

string EncoderResolver::SimpleTokenToEncoderId(const string &token)
{
	if (token == "x264" || token == "x264_lowcpu")
		return "obs_x264";
	if (token == "qsv")
		return "obs_qsv11_v2";
	if (token == "qsv_av1")
		return "obs_qsv11_av1";
	if (token == "amd")
		return "h264_texture_amf";
	if (token == "amd_hevc")
		return "h265_texture_amf";
	if (token == "amd_av1")
		return "av1_texture_amf";
	if (token == "nvenc")
		return "obs_nvenc_h264_tex";
	if (token == "nvenc_hevc")
		return "obs_nvenc_hevc_tex";
	if (token == "nvenc_av1")
		return "obs_nvenc_av1_tex";
	if (token == "apple_h264")
		return "com.apple.videotoolbox.videoencoder.ave.avc";
	if (token == "apple_hevc")
		return "com.apple.videotoolbox.videoencoder.ave.hevc";
	return string();
}

const char *EncoderResolver::SimpleTokenToPresetKey(const string &token)
{
	if (token == "qsv" || token == "qsv_av1")
		return "QSVPreset";
	if (token == "amd" || token == "amd_hevc")
		return "AMDPreset";
	if (token == "amd_av1")
		return "AMDAV1Preset";
	if (token == "nvenc" || token == "nvenc_hevc" || token == "nvenc_av1")
		return "NVENCPreset2";
	return "Preset";
}

bool EncoderResolver::IsAvailable(const string &id) const
{
	if (id.empty() || !available_)
		return false;
	return available_(id.c_str());
}

EncoderResolution EncoderResolver::Resolve(const string &requested) const
{
	EncoderResolution result;
	result.requestedId = requested;

	string candidate = SimpleTokenToEncoderId(requested);
	if (candidate.empty())
		candidate = requested;

	if (candidate.empty()) {
		result.effectiveId = FirstAvailable();
		result.changed = !result.effectiveId.empty();
		result.reason = result.effectiveId.empty()
					? "no encoder preference saved and none available"
					: "no encoder preference saved; using first available";
		return result;
	}

	if (IsAvailable(candidate)) {
		result.effectiveId = candidate;
		return result;
	}

	result.changed = true;
	result.effectiveId = FirstAvailable();
	if (result.effectiveId.empty()) {
		result.reason = "requested encoder \"" + requested + "\" unavailable and no fallback available";
	} else {
		result.reason = "requested encoder \"" + requested + "\" unavailable; using \"" +
				result.effectiveId + "\"";
	}
	return result;
}

vector<string> EncoderResolver::CandidateIds(const string &requested) const
{
	EncoderResolution resolution = Resolve(requested);

	vector<string> ids;
	if (!resolution.effectiveId.empty())
		ids.push_back(resolution.effectiveId);
	for (const string &id : Chain()) {
		if (find(ids.begin(), ids.end(), id) == ids.end())
			ids.push_back(id);
	}
	return ids;
}

string EncoderResolver::FirstAvailable() const
{
	for (const string &id : Chain()) {
		if (IsAvailable(id))
			return id;
	}
	return string();
}

} /* namespace MoonLit */
