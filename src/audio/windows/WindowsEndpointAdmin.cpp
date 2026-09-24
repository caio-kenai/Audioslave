#include "platform/windows/WinCommon.h"
#include "audio/windows/WindowsEndpointAdmin.h"
#include "audio/windows/ComHelpers.h"
#include "audio/windows/PolicyConfig.h"
#include "platform/windows/WinHandles.h"

#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>

namespace audioslave::win
{
ResultCode WindowsEndpointAdmin::setEnabled (const juce::String& endpointId, bool enabled)
{
    if (endpointId.isEmpty())
        return E_INVALIDARG;
    juce::ComSmartPtr<IPolicyConfig> policy;
    if (const HRESULT hr = policy.CoCreateInstance (policyConfigClientClsid, CLSCTX_ALL); FAILED (hr))
        return hr;
    return policy->SetEndpointVisibility (endpointId.toWideCharPointer(), enabled ? TRUE : FALSE);
}

ResultCode WindowsEndpointAdmin::getState (const juce::String& endpointId, EndpointState& out)
{
    juce::ComSmartPtr<IMMDevice> device;
    if (const HRESULT hr = openDevice (endpointId, device); FAILED (hr))
        return hr;
    DWORD state = 0;
    if (const HRESULT hr = device->GetState (&state); FAILED (hr))
        return hr;
    switch (state)
    {
        case DEVICE_STATE_ACTIVE:    out = EndpointState::active; break;
        case DEVICE_STATE_DISABLED:  out = EndpointState::disabled; break;
        case DEVICE_STATE_UNPLUGGED: out = EndpointState::unplugged; break;
        default:                     out = EndpointState::notPresent; break;
    }
    return S_OK;
}

ResultCode WindowsEndpointAdmin::getDescription (const juce::String& endpointId, juce::String& out)
{
    juce::ComSmartPtr<IMMDevice> device;
    if (const HRESULT hr = openDevice (endpointId, device); FAILED (hr))
        return hr;
    return getDeviceDescription (device, out);
}

ResultCode WindowsEndpointAdmin::setDescription (const juce::String& endpointId, const juce::String& description)
{
    const auto text = description.trim();
    if (endpointId.isEmpty() || text.isEmpty())
        return E_INVALIDARG;

    juce::ComSmartPtr<IMMDevice> device;
    HRESULT hr = openDevice (endpointId, device);
    if (FAILED (hr))
        return hr;
    juce::ComSmartPtr<IPropertyStore> store;
    if (FAILED (hr = device->OpenPropertyStore (STGM_READWRITE, store.resetAndGetPointerAddress())))
        return hr;

    PROPVARIANT value;
    ::PropVariantInit (&value);
    value.vt = VT_LPWSTR;
    value.pwszVal = const_cast<LPWSTR> (text.toWideCharPointer()); // not freed: owned by `text`
    hr = store->SetValue (PKEY_Device_DeviceDesc, value);
    if (SUCCEEDED (hr))
        hr = store->Commit();
    return hr;
}
} // namespace audioslave::win
