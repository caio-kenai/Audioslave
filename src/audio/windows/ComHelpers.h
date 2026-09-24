#pragma once
// Shared MMDevice helpers for the Windows audio implementations.
// Callers must have COM initialised on the current thread (ScopedComInit).

#include "platform/windows/WinCommon.h"

#include <mmdeviceapi.h>

namespace audioslave::win
{
HRESULT createDeviceEnumerator (juce::ComSmartPtr<IMMDeviceEnumerator>& out);
HRESULT openDevice (const juce::String& endpointId, juce::ComSmartPtr<IMMDevice>& out);
HRESULT openDevice (IMMDeviceEnumerator* enumerator, const juce::String& endpointId, juce::ComSmartPtr<IMMDevice>& out);
HRESULT getDeviceId (IMMDevice* device, juce::String& out);
HRESULT getFriendlyName (IMMDevice* device, juce::String& out);
// PKEY_Device_DeviceDesc: the editable part of the name ("Speakers").
HRESULT getDeviceDescription (IMMDevice* device, juce::String& out);
} // namespace audioslave::win
