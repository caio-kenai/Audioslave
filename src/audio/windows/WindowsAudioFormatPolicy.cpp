#include "platform/windows/WinCommon.h"
#include "audio/windows/WindowsAudioFormatPolicy.h"
#include "audio/windows/ComHelpers.h"
#include "audio/windows/PolicyConfig.h"
#include "audio/windows/WindowsFormatSupport.h"
#include "platform/windows/WinHandles.h"

#include <ks.h>
#include <ksmedia.h>
#include <propsys.h>

namespace audioslave::win
{
namespace
{
// PKEY_AudioEngine_DeviceFormat {f19f064d-082c-4e27-bc73-6882a1bb8e4c},0
constexpr PROPERTYKEY deviceFormatKey = {
    { 0xf19f064d, 0x082c, 0x4e27, { 0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c } }, 0
};
} // namespace

ResultCode WindowsAudioFormatPolicy::getDeviceFormat (const juce::String& endpointId, AudioFormat& out)
{
    juce::ComSmartPtr<IMMDevice> device;
    HRESULT hr = openDevice (endpointId, device);
    if (FAILED (hr))
        return hr;
    juce::ComSmartPtr<IPropertyStore> store;
    if (FAILED (hr = device->OpenPropertyStore (STGM_READ, store.resetAndGetPointerAddress())))
        return hr;

    PropVariant value;
    if (FAILED (hr = store->GetValue (deviceFormatKey, value.put())))
        return hr;

    const auto& v = value.get();
    if (v.vt != VT_BLOB || v.blob.pBlobData == nullptr || v.blob.cbSize < sizeof (WAVEFORMATEX))
        return HRESULT_FROM_WIN32 (ERROR_INVALID_DATA);

    const auto* wave = reinterpret_cast<const WAVEFORMATEX*> (v.blob.pBlobData);
    out = AudioFormat {};
    out.sampleRate = wave->nSamplesPerSec;
    out.containerBits = wave->wBitsPerSample;
    out.validBits = wave->wBitsPerSample;
    out.channels = wave->nChannels;
    out.channelMask = wave->nChannels == 1 ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    out.isFloat = wave->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (wave->wFormatTag == WAVE_FORMAT_EXTENSIBLE && v.blob.cbSize >= sizeof (WAVEFORMATEXTENSIBLE))
    {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*> (wave);
        if (ext->Samples.wValidBitsPerSample != 0)
            out.validBits = ext->Samples.wValidBitsPerSample;
        out.channelMask = ext->dwChannelMask;
        out.isFloat = ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    return S_OK;
}

ResultCode WindowsAudioFormatPolicy::isFormatSupported (const juce::String& endpointId, const AudioFormat& format,
                                                        bool& supported)
{
    supported = false;
    juce::ComSmartPtr<IMMDevice> device;
    if (const HRESULT hr = openDevice (endpointId, device); FAILED (hr))
        return hr;

    WindowsFormatSupport support;
    if (const HRESULT hr = WindowsFormatSupport::open (device, support); FAILED (hr))
        return hr;
    return support.isSupported (format, supported);
}

ResultCode WindowsAudioFormatPolicy::probeFormats (const juce::String& endpointId, const std::vector<AudioFormat>& formats,
                                                   std::vector<bool>& supported)
{
    supported.assign (formats.size(), false);
    juce::ComSmartPtr<IMMDevice> device;
    if (const HRESULT hr = openDevice (endpointId, device); FAILED (hr))
        return hr;

    // One walk of the device topology for the whole list.
    WindowsFormatSupport support;
    if (const HRESULT hr = WindowsFormatSupport::open (device, support); FAILED (hr))
        return hr;
    for (size_t i = 0; i < formats.size(); ++i)
    {
        bool ok = false;
        if (const HRESULT hr = support.isSupported (formats[i], ok); FAILED (hr))
            return hr;
        supported[i] = ok;
    }
    return S_OK;
}

ResultCode WindowsAudioFormatPolicy::setDeviceFormat (const juce::String& endpointId, const AudioFormat& format)
{
    if (endpointId.isEmpty())
        return E_INVALIDARG;

    juce::ComSmartPtr<IPolicyConfig> policy;
    if (const HRESULT hr = policy.CoCreateInstance (policyConfigClientClsid, CLSCTX_ALL); FAILED (hr))
        return hr;

    auto deviceFormat = toWaveFormat (format);
    // The shared-mode mix format is always 32-bit float at the device rate.
    AudioFormat mix = format;
    mix.containerBits = 32;
    mix.validBits = 32;
    mix.isFloat = true;
    auto mixFormat = toWaveFormat (mix);

    return policy->SetDeviceFormat (endpointId.toWideCharPointer(), reinterpret_cast<WAVEFORMATEX*> (&deviceFormat),
                                    reinterpret_cast<WAVEFORMATEX*> (&mixFormat));
}
} // namespace audioslave::win
