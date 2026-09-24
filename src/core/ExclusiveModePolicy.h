#pragma once
// Exclusive Mode Protection policy: inspect an endpoint and, when enforcing,
// turn "allow exclusive control" off, then read it back to verify.

#include "audio/AudioInterfaces.h"

namespace audioslave
{
enum class ExclusiveResult
{
    unknown,      // the store could not be read: nothing done, retry later
    alreadyOff,   // exclusive mode already blocked
    fixed,        // was allowed; switched off and verified
    writeFailed,  // was allowed; the write was rejected
    verifyFailed, // the write succeeded but the read-back still says allowed
    skipped       // allowed, but not changed (enforcement off / backoff / paused)
};

juce::String exclusiveResultName (ExclusiveResult r);

class ExclusiveModePolicy
{
public:
    explicit ExclusiveModePolicy (IExclusiveModeStore& store) : store_ (store) {}

    // Fills endpoint.exclusive from the store. Returns the read result.
    ResultCode inspect (AudioEndpoint& endpoint);

    // Never writes when the state cannot be read; always verifies a write.
    ExclusiveResult judgeAndFix (AudioEndpoint& endpoint, bool enforce);

private:
    IExclusiveModeStore& store_;
};
} // namespace audioslave
