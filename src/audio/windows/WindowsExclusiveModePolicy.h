#pragma once
// IExclusiveModeStore backed by the endpoint property store (MMDevice API).
//
// The keys are the ones the Sound control panel (mmsys.cpl) toggles:
//   {B3F8FA53-0004-438E-9003-51A46E139BFC},3  "Allow applications to take
//                                              exclusive control of this device"
//   {B3F8FA53-0004-438E-9003-51A46E139BFC},4  "Give exclusive mode applications
//                                              priority"
// stored as VT_UI4 (0 = off). They are not in the public SDK headers; their
// behaviour is verified: with 0 written, IAudioClient::Initialize(EXCLUSIVE)
// fails with AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED. A missing value (VT_EMPTY)
// means the Windows default, which is *allowed*.

#include "audio/AudioInterfaces.h"

namespace audioslave::win
{
class WindowsExclusiveModePolicy final : public IExclusiveModeStore
{
public:
    ResultCode read (const juce::String& endpointId, bool& allow, bool& priority) override;
    ResultCode write (const juce::String& endpointId, bool allow, bool priority) override;

    // Self-test for `diagnose`: reads the current value and writes the very
    // same value back through a STGM_READWRITE store.
    ResultCode verifyWritable (const juce::String& endpointId);
};
} // namespace audioslave::win
