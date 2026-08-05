#pragma once

#include <QString>

struct IMMDevice;
struct IAudioEndpointVolume;

namespace MoonLit {

/* Windows Core Audio endpoint volume (IAudioEndpointVolume) control for a
 * device picked by id, or for the system default endpoint of the direction.
 * MoonLit uses this to adjust the physical input/output device volume, which
 * is what the user hears, independently of the per-track recording levels
 * controlled by the dashboard mixer. */
class EndpointVolume {
public:
	enum class Direction { Capture, Render };

	EndpointVolume() = default;
	~EndpointVolume();

	EndpointVolume(const EndpointVolume &) = delete;
	EndpointVolume &operator=(const EndpointVolume &) = delete;

	/* Resolves the endpoint and opens the volume control. An empty id or
	 * "default" resolves the system default (input: communications role,
	 * output: console role, matching win-wasapi). False when the device no
	 * longer exists. */
	bool open(Direction direction, const QString &deviceId);
	void close();
	bool isOpen() const { return volume_ != nullptr; }

	/* Scalar volume in [0, 1]; -1.0 when not open. */
	float scalar() const;
	bool setScalar(float value);
	bool muted() const;
	bool setMuted(bool muted);
	QString deviceName() const;

private:
	IMMDevice *device_ = nullptr;
	IAudioEndpointVolume *volume_ = nullptr;
	QString name_;
	bool comInitialized_ = false;
};

} // namespace MoonLit
