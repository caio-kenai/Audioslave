#pragma once
// In-memory test doubles for the audio interfaces.

#include "audio/AudioInterfaces.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <map>
#include <vector>

namespace audioslave::test
{
inline AudioEndpoint endpoint (const juce::String& id, EndpointFlow flow = EndpointFlow::render,
                               EndpointState state = EndpointState::active)
{
    AudioEndpoint e;
    e.id = id;
    e.name = id;
    e.flow = flow;
    e.state = state;
    return e;
}

inline AudioFormat fmt (std::uint32_t rate, std::uint16_t valid, std::uint16_t container, bool isFloat = false)
{
    AudioFormat f;
    f.sampleRate = rate;
    f.validBits = valid;
    f.containerBits = container;
    f.isFloat = isFloat;
    return f;
}

class MockEnumerator : public IAudioEndpointEnumerator
{
public:
    std::vector<AudioEndpoint> endpoints;
    ResultCode result = result::ok;
    std::atomic<int> calls { 0 };

    ResultCode enumerate (std::vector<AudioEndpoint>& out) override
    {
        ++calls;
        if (failed (result))
            return result;
        out = endpoints;
        return out.empty() ? result::notFound : result::ok;
    }
};

class MockExclusiveStore : public IExclusiveModeStore
{
public:
    std::map<juce::String, bool> allow;
    std::map<juce::String, bool> priority;
    ResultCode readResult = result::ok;
    ResultCode writeResult = result::ok;
    bool writesStick = true;
    std::atomic<int> writeCalls { 0 };
    std::function<void()> onWrite; // runs inside write() (tests block here)

    ResultCode read (const juce::String& id, bool& a, bool& p) override
    {
        if (failed (readResult))
            return readResult;
        a = allow.count (id) != 0 && allow[id];
        p = priority.count (id) != 0 && priority[id];
        return result::ok;
    }

    ResultCode write (const juce::String& id, bool a, bool p) override
    {
        ++writeCalls;
        if (onWrite)
            onWrite();
        if (failed (writeResult))
            return writeResult;
        if (writesStick)
        {
            allow[id] = a;
            priority[id] = p;
        }
        return result::ok;
    }
};

class MockFormatStore : public IAudioFormatStore
{
public:
    struct Layout
    {
        std::uint32_t rate;
        std::uint16_t valid;
        std::uint16_t container;
        bool isFloat;
    };

    std::map<juce::String, AudioFormat> current;
    std::vector<Layout> supported;
    ResultCode readResult = result::ok;
    ResultCode supportResult = result::ok;
    ResultCode writeResult = result::ok;
    bool writesStick = true;
    std::atomic<int> writeCalls { 0 };
    AudioFormat lastWritten;

    ResultCode getDeviceFormat (const juce::String& id, AudioFormat& out) override
    {
        if (failed (readResult))
            return readResult;
        const auto it = current.find (id);
        if (it == current.end())
            return result::notFound;
        out = it->second;
        return result::ok;
    }

    ResultCode isFormatSupported (const juce::String&, const AudioFormat& f, bool& ok) override
    {
        ok = false;
        if (failed (supportResult))
            return supportResult;
        for (const auto& l : supported)
            if (l.rate == f.sampleRate && l.valid == f.validBits && l.container == f.containerBits && l.isFloat == f.isFloat)
                ok = true;
        return result::ok;
    }

    ResultCode setDeviceFormat (const juce::String& id, const AudioFormat& f) override
    {
        ++writeCalls;
        lastWritten = f;
        if (failed (writeResult))
            return writeResult;
        if (writesStick)
            current[id] = f;
        return result::ok;
    }
};

// Unique names for pipes / temp files so parallel runs never collide.
inline juce::String uniqueName (const juce::String& prefix)
{
    return prefix + "." + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
}

inline juce::File tempFile (const juce::String& name)
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (uniqueName (name));
}

// Polls `condition` for up to `timeoutMs`.
inline bool waitUntil (const std::function<bool()>& condition, int timeoutMs = 5000)
{
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (! condition())
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        juce::Thread::sleep (10);
    }
    return true;
}
} // namespace audioslave::test
