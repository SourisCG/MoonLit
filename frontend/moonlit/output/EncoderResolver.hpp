#pragma once

#include <functional>
#include <string>
#include <vector>

namespace MoonLit {

/* Result of resolving a requested encoder to one that is actually usable. */
struct EncoderResolution {
	std::string requestedId; /* token or id as configured */
	std::string effectiveId; /* obs encoder id to use; empty when nothing is available */
	std::string reason;      /* human-readable explanation when the request changed */
	bool changed = false;    /* true when effectiveId differs from what was requested */
};

/* Deterministic encoder resolution for MoonLit.
 *
 * Maps simple output tokens to obs encoder ids and verifies availability.
 * When the requested encoder is unavailable the fixed fallback chain
 * NVENC -> QSV -> AMF -> x264 is used. Resolution never writes configuration. */
class EncoderResolver {
public:
	using AvailableFn = std::function<bool(const char *)>;

	explicit EncoderResolver(AvailableFn available);

	/* Maps SIMPLE_ENCODER_* style tokens to obs encoder ids.
	 * Returns an empty string for ids that are not simple tokens. */
	static std::string SimpleTokenToEncoderId(const std::string &token);

	/* Maps a simple token or raw obs encoder id to the SimpleOutput config
	 * key that holds its preset, matching SimpleOutput::Update():
	 * QSVPreset / AMDPreset / AMDAV1Preset / NVENCPreset2 / Preset. */
	static const char *SimpleTokenToPresetKey(const std::string &token);

	/* The fixed fallback chain, best first. */
	static const std::vector<std::string> &FallbackChain();

	/* Resolves a requested simple token or obs encoder id. */
	EncoderResolution Resolve(const std::string &requested) const;

	/* Ordered candidate ids to attempt at runtime: the resolved request
	 * first, then the remaining fallback chain as a safety net. */
	std::vector<std::string> CandidateIds(const std::string &requested) const;

	/* First id in the fallback chain that is currently available. */
	std::string FirstAvailable() const;

private:
	bool IsAvailable(const std::string &id) const;

	AvailableFn available_;
};

} /* namespace MoonLit */
