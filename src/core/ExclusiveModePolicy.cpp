#include "core/ExclusiveModePolicy.h"

namespace audioslave
{
juce::String exclusiveResultName (ExclusiveResult r)
{
    switch (r)
    {
        case ExclusiveResult::unknown:      return "unknown";
        case ExclusiveResult::alreadyOff:   return "off";
        case ExclusiveResult::fixed:        return "fixed";
        case ExclusiveResult::writeFailed:  return "write-failed";
        case ExclusiveResult::verifyFailed: return "verify-failed";
        case ExclusiveResult::skipped:      return "skipped";
    }
    return "unknown";
}

ResultCode ExclusiveModePolicy::inspect (AudioEndpoint& endpoint)
{
    bool allow = false;
    bool priority = false;
    const auto rc = store_.read (endpoint.id, allow, priority);
    endpoint.exclusive.readResult = rc;
    endpoint.exclusive.known = succeeded (rc);
    endpoint.exclusive.allowed = succeeded (rc) && allow;
    endpoint.exclusive.priority = succeeded (rc) && priority;
    return rc;
}

ExclusiveResult ExclusiveModePolicy::judgeAndFix (AudioEndpoint& endpoint, bool enforce)
{
    if (failed (inspect (endpoint)))
        return ExclusiveResult::unknown;
    if (! endpoint.exclusive.allowed)
        return ExclusiveResult::alreadyOff;
    if (! enforce)
        return ExclusiveResult::skipped;

    if (failed (store_.write (endpoint.id, false, false)))
        return ExclusiveResult::writeFailed;

    // Verify the write stuck (the endpoint keeps the verified state).
    if (failed (inspect (endpoint)) || endpoint.exclusive.allowed)
        return ExclusiveResult::verifyFailed;
    return ExclusiveResult::fixed;
}
} // namespace audioslave
