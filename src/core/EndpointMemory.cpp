#include "core/EndpointMemory.h"

namespace audioslave
{
EndpointMemory::EndpointMemory (Clock clock) : clock_ (std::move (clock))
{
    if (! clock_)
        clock_ = [] { return static_cast<juce::int64> (juce::Time::getMillisecondCounterHiRes()); };
}

bool EndpointMemory::inBackoff (const juce::String& endpointId) const
{
    const juce::ScopedLock sl (lock_);
    const auto it = failures_.find (endpointId);
    return it != failures_.end() && clock_() - it->second < failureBackoffMs;
}

void EndpointMemory::recordFailure (const juce::String& endpointId)
{
    const juce::ScopedLock sl (lock_);
    failures_[endpointId] = clock_();
}

void EndpointMemory::clearFailure (const juce::String& endpointId)
{
    const juce::ScopedLock sl (lock_);
    failures_.erase (endpointId);
}

bool EndpointMemory::notice (const juce::String& key, const juce::String& message)
{
    const juce::ScopedLock sl (lock_);
    auto [it, inserted] = notices_.try_emplace (key, message);
    if (inserted)
        return true;
    if (it->second == message)
        return false;
    it->second = message;
    return true;
}

void EndpointMemory::clearNotice (const juce::String& key)
{
    const juce::ScopedLock sl (lock_);
    notices_.erase (key);
}

void EndpointMemory::forgetEndpoint (const juce::String& endpointId)
{
    const juce::ScopedLock sl (lock_);
    failures_.erase (endpointId);
    for (auto it = notices_.begin(); it != notices_.end();)
        it = it->first.contains (endpointId) ? notices_.erase (it) : std::next (it);
}

void EndpointMemory::clear()
{
    const juce::ScopedLock sl (lock_);
    failures_.clear();
    notices_.clear();
}
} // namespace audioslave
