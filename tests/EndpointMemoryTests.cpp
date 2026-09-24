#include "core/EndpointMemory.h"
#include "tests/Mocks.h"

namespace audioslave::test
{
class EndpointMemoryTests final : public juce::UnitTest
{
public:
    EndpointMemoryTests() : juce::UnitTest ("Endpoint memory", "Core") {}

    void runTest() override
    {
        juce::int64 now = 1000;
        EndpointMemory memory ([&now] { return now; });

        beginTest ("Failure backoff lasts 30 s");
        {
            expect (! memory.inBackoff ("a"));
            memory.recordFailure ("a");
            expect (memory.inBackoff ("a"));
            now += EndpointMemory::failureBackoffMs - 1;
            expect (memory.inBackoff ("a"));
            now += 1;
            expect (! memory.inBackoff ("a"));
            memory.recordFailure ("a");
            memory.clearFailure ("a");
            expect (! memory.inBackoff ("a"));
        }

        beginTest ("A notice is reported once, again when it changes");
        {
            expect (memory.notice ("fmt|a", "unsupported"));
            expect (! memory.notice ("fmt|a", "unsupported"));
            expect (memory.notice ("fmt|a", "unknown"));
            memory.clearNotice ("fmt|a");
            expect (memory.notice ("fmt|a", "unknown"));
        }

        beginTest ("A (re)created endpoint is forgotten");
        {
            memory.recordFailure ("a");
            memory.notice ("excl|a", "x");
            memory.notice ("excl|b", "y");
            memory.forgetEndpoint ("a");
            expect (! memory.inBackoff ("a"));
            expect (memory.notice ("excl|a", "x"));
            expect (! memory.notice ("excl|b", "y"));
        }

        beginTest ("clear() forgets everything");
        {
            memory.recordFailure ("b");
            memory.clear();
            expect (! memory.inBackoff ("b"));
            expect (memory.notice ("excl|b", "y"));
        }
    }
};

static EndpointMemoryTests endpointMemoryTests;
} // namespace audioslave::test
