#include "core/ExclusiveModePolicy.h"
#include "tests/Mocks.h"

namespace audioslave::test
{
class ExclusiveModePolicyTests final : public juce::UnitTest
{
public:
    ExclusiveModePolicyTests() : juce::UnitTest ("Exclusive mode policy", "Audio") {}

    void runTest() override
    {
        beginTest ("Already off: nothing written");
        {
            MockExclusiveStore store;
            store.allow["d"] = false;
            ExclusiveModePolicy policy (store);
            auto e = endpoint ("d");
            expect (policy.judgeAndFix (e, true) == ExclusiveResult::alreadyOff);
            expectEquals (store.writeCalls.load(), 0);
            expect (e.exclusive.known && ! e.exclusive.allowed);
        }

        beginTest ("Allowed: switched off and verified");
        {
            MockExclusiveStore store;
            store.allow["d"] = true;
            store.priority["d"] = true;
            ExclusiveModePolicy policy (store);
            auto e = endpoint ("d");
            expect (policy.judgeAndFix (e, true) == ExclusiveResult::fixed);
            expectEquals (store.writeCalls.load(), 1);
            expect (! store.allow["d"]);
            expect (! store.priority["d"]);
            expect (! e.exclusive.allowed); // the endpoint keeps the verified state
        }

        beginTest ("Report only when not enforcing");
        {
            MockExclusiveStore store;
            store.allow["d"] = true;
            ExclusiveModePolicy policy (store);
            auto e = endpoint ("d");
            expect (policy.judgeAndFix (e, false) == ExclusiveResult::skipped);
            expectEquals (store.writeCalls.load(), 0);
            expect (store.allow["d"]);
        }

        beginTest ("Unreadable state: never written blindly");
        {
            MockExclusiveStore store;
            store.readResult = result::fail;
            ExclusiveModePolicy policy (store);
            auto e = endpoint ("d");
            expect (policy.judgeAndFix (e, true) == ExclusiveResult::unknown);
            expectEquals (store.writeCalls.load(), 0);
            expect (! e.exclusive.known);
            expect (e.exclusive.readResult == result::fail);
        }

        beginTest ("Rejected write");
        {
            MockExclusiveStore store;
            store.allow["d"] = true;
            store.writeResult = result::accessDenied;
            ExclusiveModePolicy policy (store);
            auto e = endpoint ("d");
            expect (policy.judgeAndFix (e, true) == ExclusiveResult::writeFailed);
        }

        beginTest ("Write that does not stick (the driver restores it)");
        {
            MockExclusiveStore store;
            store.allow["d"] = true;
            store.writesStick = false;
            ExclusiveModePolicy policy (store);
            auto e = endpoint ("d");
            expect (policy.judgeAndFix (e, true) == ExclusiveResult::verifyFailed);
            expectEquals (store.writeCalls.load(), 1);
        }
    }
};

static ExclusiveModePolicyTests exclusiveModePolicyTests;
} // namespace audioslave::test
