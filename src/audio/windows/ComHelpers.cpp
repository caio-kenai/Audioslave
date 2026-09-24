#include "platform/windows/WinCommon.h"
#include "audio/windows/ComHelpers.h"
#include "platform/windows/WinHandles.h"

#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>

namespace audioslave::win
{
HRESULT createDeviceEnumerator (juce::ComSmartPtr<IMMDeviceEnumerator>& out)
{
    return out.CoCreateInstance (__uuidof (MMDeviceEnumerator), CLSCTX_INPROC_SERVER);
}

HRESULT openDevice (IMMDeviceEnumerator* enumerator, const juce::String& endpointId, juce::ComSmartPtr<IMMDevice>& out)
{
    if (enumerator == nullptr)
        return E_POINTER;
    if (endpointId.isEmpty())
        return E_INVALIDARG;
    return enumerator->GetDevice (endpointId.toWideCharPointer(), out.resetAndGetPointerAddress());
}

HRESULT openDevice (const juce::String& endpointId, juce::ComSmartPtr<IMMDevice>& out)
{
    juce::ComSmartPtr<IMMDeviceEnumerator> enumerator;
    if (const HRESULT hr = createDeviceEnumerator (enumerator); FAILED (hr))
        return hr;
    return openDevice (enumerator, endpointId, out);
}

HRESULT getDeviceId (IMMDevice* device, juce::String& out)
{
    CoTaskMemPtr<wchar_t> id;
    const HRESULT hr = device->GetId (id.put());
    if (SUCCEEDED (hr))
        out = id ? juce::String (id.get()) : juce::String();
    return hr;
}

HRESULT getFriendlyName (IMMDevice* device, juce::String& out)
{
    juce::ComSmartPtr<IPropertyStore> store;
    HRESULT hr = device->OpenPropertyStore (STGM_READ, store.resetAndGetPointerAddress());
    if (FAILED (hr))
    {
        out = "(unavailable)";
        return hr;
    }
    PropVariant value;
    hr = store->GetValue (PKEY_Device_FriendlyName, value.put());
    if (SUCCEEDED (hr) && value.get().vt == VT_LPWSTR && value.get().pwszVal != nullptr)
    {
        out = juce::String (value.get().pwszVal);
        return S_OK;
    }
    out = "(unnamed)";
    return FAILED (hr) ? hr : E_FAIL;
}
} // namespace audioslave::win
