#include "platform/windows/WinCommon.h"
#include "audio/windows/WindowsExclusiveModePolicy.h"
#include "audio/windows/ComHelpers.h"
#include "platform/windows/WinHandles.h"

#include <propsys.h>

namespace audioslave::win
{
namespace
{
constexpr PROPERTYKEY exclusiveAllowedKey = {
    { 0xB3F8FA53, 0x0004, 0x438E, { 0x90, 0x03, 0x51, 0xA4, 0x6E, 0x13, 0x9B, 0xFC } }, 3
};
constexpr PROPERTYKEY exclusivePriorityKey = {
    { 0xB3F8FA53, 0x0004, 0x438E, { 0x90, 0x03, 0x51, 0xA4, 0x6E, 0x13, 0x9B, 0xFC } }, 4
};

HRESULT openStore (const juce::String& endpointId, DWORD access, juce::ComSmartPtr<IPropertyStore>& store)
{
    juce::ComSmartPtr<IMMDevice> device;
    if (const HRESULT hr = openDevice (endpointId, device); FAILED (hr))
        return hr;
    return device->OpenPropertyStore (access, store.resetAndGetPointerAddress());
}

HRESULT readFlag (IPropertyStore* store, const PROPERTYKEY& key, bool& out)
{
    PropVariant value;
    const HRESULT hr = store->GetValue (key, value.put());
    if (FAILED (hr))
        return hr;
    if (value.get().vt == VT_UI4)
    {
        out = value.get().ulVal != 0;
        return S_OK;
    }
    if (value.get().vt == VT_EMPTY)
    {
        // Absent -> Windows default, which is "allowed" (verified: a real
        // exclusive stream opens on such endpoints, e.g. NDI devices).
        out = true;
        return S_OK;
    }
    return HRESULT_FROM_WIN32 (ERROR_INVALID_DATA);
}

HRESULT writeFlag (IPropertyStore* store, const PROPERTYKEY& key, bool value)
{
    PropVariant v;
    v.get().vt = VT_UI4;
    v.get().ulVal = value ? 1UL : 0UL;
    return store->SetValue (key, v.get());
}
} // namespace

ResultCode WindowsExclusiveModePolicy::read (const juce::String& endpointId, bool& allow, bool& priority)
{
    allow = false;
    priority = false;

    juce::ComSmartPtr<IPropertyStore> store;
    if (const HRESULT hr = openStore (endpointId, STGM_READ, store); FAILED (hr))
        return hr;

    // "Allow" is the authoritative key: if it cannot be read, report the
    // failure so the engine retries later instead of writing blindly.
    if (const HRESULT hr = readFlag (store, exclusiveAllowedKey, allow); FAILED (hr))
        return hr;
    return readFlag (store, exclusivePriorityKey, priority);
}

ResultCode WindowsExclusiveModePolicy::write (const juce::String& endpointId, bool allow, bool priority)
{
    juce::ComSmartPtr<IPropertyStore> store;
    HRESULT hr = openStore (endpointId, STGM_READWRITE, store);
    if (SUCCEEDED (hr))
        hr = writeFlag (store, exclusiveAllowedKey, allow);
    if (SUCCEEDED (hr))
        hr = writeFlag (store, exclusivePriorityKey, priority);
    if (SUCCEEDED (hr))
        hr = store->Commit();
    return hr;
}

ResultCode WindowsExclusiveModePolicy::verifyWritable (const juce::String& endpointId)
{
    bool allow = false;
    bool priority = false;
    if (const HRESULT hr = read (endpointId, allow, priority); FAILED (hr))
        return hr;

    juce::ComSmartPtr<IPropertyStore> store;
    HRESULT hr = openStore (endpointId, STGM_READWRITE, store);
    if (SUCCEEDED (hr))
        hr = writeFlag (store, exclusiveAllowedKey, allow);
    if (SUCCEEDED (hr))
        hr = store->Commit();
    return hr;
}
} // namespace audioslave::win
