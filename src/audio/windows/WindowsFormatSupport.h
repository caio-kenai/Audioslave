#pragma once
// Asks the *driver* whether it can stream a format, through IKsFormatSupport
// on the endpoint's streaming (Software_IO) KS pin, reached by walking
// IDeviceTopology from the endpoint to the wave filter.
//
// This is the only reliable source while exclusive mode is blocked:
// IAudioClient::IsFormatSupported(EXCLUSIVE) - and therefore JUCE's WASAPI
// exclusive device type - fails with AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED
// once Audioslave has done its job.

#include "platform/windows/WinCommon.h"
#include "audio/models/AudioFormat.h"

#include <mmdeviceapi.h>
#include <mmreg.h>
#include <devicetopology.h>

namespace audioslave::win
{
// WAVEFORMATEXTENSIBLE (PCM or IEEE float) describing `format`.
WAVEFORMATEXTENSIBLE toWaveFormat (const AudioFormat& format);

class WindowsFormatSupport
{
public:
    // Locates IKsFormatSupport for `device`. Returns an HRESULT
    // (E_NOINTERFACE when the driver exposes no streaming pin).
    static HRESULT open (IMMDevice* device, WindowsFormatSupport& out);

    // S_OK and `supported` set, or a failure when it cannot be determined.
    HRESULT isSupported (const AudioFormat& format, bool& supported) const;

private:
    juce::ComSmartPtr<IKsFormatSupport> formatSupport_;
};
} // namespace audioslave::win
