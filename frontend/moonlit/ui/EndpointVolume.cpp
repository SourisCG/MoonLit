#include "EndpointVolume.hpp"

#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <propkey.h>

#include <QString>

#include <string>

#pragma comment(lib, "ole32.lib")

/* PKEY_Device_FriendlyName defined inline to avoid the DEFINE_PROPERTYKEY
 * header conflict between propkey.h and functiondiscoverykeys_devpkey.h
 * (same approach as AudioInputDevices() in the settings dialog). */
static const PROPERTYKEY kDeviceFriendlyName = {
	{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

namespace MoonLit {

EndpointVolume::~EndpointVolume()
{
	close();
}

bool EndpointVolume::open(Direction direction, const QString &deviceId)
{
	close();

	comInitialized_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) == S_OK;

	IMMDeviceEnumerator *enumerator = nullptr;
	if (CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
			     __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&enumerator)) != S_OK) {
		return false;
	}

	const EDataFlow flow = direction == Direction::Capture ? eCapture : eRender;
	IMMDevice *device = nullptr;
	const std::wstring id = deviceId.toStdWString();
	if (deviceId.isEmpty() || deviceId == QStringLiteral("default")) {
		const ERole role = direction == Direction::Capture ? eCommunications : eConsole;
		if (FAILED(enumerator->GetDefaultAudioEndpoint(flow, role, &device))) {
			enumerator->Release();
			return false;
		}
	} else if (FAILED(enumerator->GetDevice(id.c_str(), &device))) {
		enumerator->Release();
		return false;
	}
	enumerator->Release();

	IAudioEndpointVolume *volume = nullptr;
	if (FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
				    reinterpret_cast<void **>(&volume)))) {
		device->Release();
		return false;
	}

	IPropertyStore *store = nullptr;
	if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store))) {
		PROPVARIANT value;
		PropVariantInit(&value);
		if (SUCCEEDED(store->GetValue(kDeviceFriendlyName, &value)) && value.vt == VT_LPWSTR) {
			name_ = QString::fromWCharArray(value.pwszVal);
		}
		PropVariantClear(&value);
		store->Release();
	}

	device_ = device;
	volume_ = volume;
	return true;
}

void EndpointVolume::close()
{
	if (volume_) {
		volume_->Release();
		volume_ = nullptr;
	}
	if (device_) {
		device_->Release();
		device_ = nullptr;
	}
	name_.clear();
	if (comInitialized_) {
		CoUninitialize();
		comInitialized_ = false;
	}
}

float EndpointVolume::scalar() const
{
	if (!volume_) {
		return -1.0f;
	}
	float value = 0.0f;
	return SUCCEEDED(volume_->GetMasterVolumeLevelScalar(&value)) ? value : -1.0f;
}

bool EndpointVolume::setScalar(float value)
{
	return volume_ && SUCCEEDED(volume_->SetMasterVolumeLevelScalar(value, nullptr));
}

bool EndpointVolume::muted() const
{
	if (!volume_) {
		return false;
	}
	BOOL value = FALSE;
	return SUCCEEDED(volume_->GetMute(&value)) && value != FALSE;
}

bool EndpointVolume::setMuted(bool muted)
{
	return volume_ && SUCCEEDED(volume_->SetMute(muted ? TRUE : FALSE, nullptr));
}

QString EndpointVolume::deviceName() const
{
	return name_;
}

} // namespace MoonLit
