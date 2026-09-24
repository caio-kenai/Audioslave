#include "core/WatchdogEngine.h"
#include "tests/Mocks.h"

#include <thread>

namespace audioslave::test
{
class WatchdogEngineTests final : public juce::UnitTest
{
public:
    WatchdogEngineTests() : juce::UnitTest ("Watchdog engine", "Core") {}

    static Configuration config()
    {
        Configuration cfg;
        cfg.checkIntervalSeconds = 3600; // tests trigger scans explicitly
        return cfg;
    }

    static WatchdogEngine::Options fastOptions()
    {
        WatchdogEngine::Options o;
        o.debounceMs = 20;
        o.enumerationRetryMs = 50;
        return o;
    }

    struct RecordingListener : WatchdogEngine::Listener
    {
        std::atomic<int> scans { 0 };
        juce::CriticalSection lock;
        juce::Array<EngineState> states;
        void engineStateChanged (EngineState s) override
        {
            const juce::ScopedLock sl (lock);
            states.add (s);
        }
        void scanCompleted (const ScanReport&) override { ++scans; }
    };

    void runTest() override
    {
        beginTest ("No endpoints is not an error");
        {
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            WatchdogEngine engine (en, ex, fm, config());
            const auto r = engine.scanOnce();
            expectEquals (r.endpointsScanned, 0);
            expectEquals (r.errors(), 0);
        }

        beginTest ("Fixes an endpoint that allows exclusive mode, leaves compliant ones alone");
        {
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            en.endpoints = { endpoint ("r1"), endpoint ("c1", EndpointFlow::capture) };
            ex.allow["r1"] = true;
            ex.allow["c1"] = false;
            WatchdogEngine engine (en, ex, fm, config());
            const auto r = engine.scanOnce();
            expectEquals (r.endpointsScanned, 2);
            expectEquals (r.exclusiveFixed, 1);
            expectEquals (r.exclusiveAlreadyOff, 1);
            expectEquals (r.errors(), 0);
            expectEquals (ex.writeCalls.load(), 1);
            expect (! ex.allow["r1"]);
        }

        beginTest ("Unplugged endpoints are protected too");
        {
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            en.endpoints = { endpoint ("jack", EndpointFlow::render, EndpointState::unplugged) };
            ex.allow["jack"] = true;
            WatchdogEngine engine (en, ex, fm, config());
            expectEquals (engine.scanOnce().exclusiveFixed, 1);
        }

        beginTest ("Respects the playback / capture filters");
        {
            auto cfg = config();
            cfg.monitorCapture = false;
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            en.endpoints = { endpoint ("r1"), endpoint ("c1", EndpointFlow::capture) };
            ex.allow["r1"] = true;
            ex.allow["c1"] = true;
            WatchdogEngine engine (en, ex, fm, cfg);
            const auto r = engine.scanOnce();
            expectEquals (r.endpointsScanned, 1);
            expectEquals (r.exclusiveFixed, 1);
            expect (ex.allow["c1"]);
        }

        beginTest ("Report only when enforcement is off");
        {
            auto cfg = config();
            cfg.enforce = false;
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            en.endpoints = { endpoint ("r1") };
            ex.allow["r1"] = true;
            WatchdogEngine engine (en, ex, fm, cfg);
            expectEquals (engine.scanOnce().exclusiveSkipped, 1);
            expectEquals (ex.writeCalls.load(), 0);
        }

        beginTest ("Enumeration failure is reported");
        {
            MockEnumerator en;
            en.result = result::fail;
            MockExclusiveStore ex;
            MockFormatStore fm;
            WatchdogEngine engine (en, ex, fm, config());
            const auto r = engine.scanOnce();
            expect (r.enumerationFailed);
            expectEquals (r.errors(), 1);
        }

        beginTest ("Paused: nothing is modified; resume fixes");
        {
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            en.endpoints = { endpoint ("r1") };
            ex.allow["r1"] = true;
            WatchdogEngine engine (en, ex, fm, config());
            expect (engine.getState() == EngineState::running);
            engine.pause();
            expect (engine.getState() == EngineState::paused);
            expect (engine.scanOnce().paused);
            expectEquals (ex.writeCalls.load(), 0);
            engine.resume();
            expect (engine.getState() == EngineState::running);
            expectEquals (engine.scanOnce().exclusiveFixed, 1);
        }

        beginTest ("pause() waits for a change that is already in progress");
        {
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            en.endpoints = { endpoint ("r1"), endpoint ("r2") };
            ex.allow["r1"] = true;
            ex.allow["r2"] = true;
            juce::WaitableEvent inWrite, release;
            ex.onWrite = [&]
            {
                inWrite.signal();
                release.wait (5000);
            };
            WatchdogEngine engine (en, ex, fm, config());

            ScanReport report;
            std::thread scanner ([&] { report = engine.scanOnce(); });
            expect (inWrite.wait (5000));

            std::atomic<bool> pauseReturned { false };
            std::thread pauser ([&] { engine.pause(); pauseReturned = true; });
            juce::Thread::sleep (150);
            expect (! pauseReturned.load(), "pause() returned while a write was in progress");

            release.signal();
            pauser.join();
            scanner.join();
            expect (pauseReturned.load());
            expectEquals (report.exclusiveFixed, 1);   // the in-flight change completed...
            expect (report.paused);                     // ...and the second endpoint was not touched
            expectEquals (ex.writeCalls.load(), 1);
            expect (ex.allow["r2"]);
        }

        beginTest ("State transitions (worker thread)");
        {
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            RecordingListener listener;
            WatchdogEngine engine (en, ex, fm, config(), fastOptions());
            engine.addListener (&listener);
            expect (engine.start());
            engine.resume(); // no-op while running
            expect (engine.getState() == EngineState::running);
            engine.pause();
            engine.pause(); // idempotent
            expect (engine.getState() == EngineState::paused);
            engine.stop();
            expect (engine.getState() == EngineState::stopped);
            engine.stop(); // idempotent
            engine.removeListener (&listener);
            const juce::ScopedLock sl (listener.lock);
            expect (listener.states.contains (EngineState::paused));
            expect (listener.states.getLast() == EngineState::stopped);
        }

        beginTest ("The worker scans at start and after a device notification");
        {
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            en.endpoints = { endpoint ("r1") };
            RecordingListener listener;
            WatchdogEngine engine (en, ex, fm, config(), fastOptions());
            engine.addListener (&listener);
            expect (engine.start());
            expect (waitUntil ([&] { return listener.scans.load() >= 1; }));

            DeviceChange change;
            change.kind = DeviceChange::Kind::propertyChanged;
            change.endpointId = "r1";
            engine.onDeviceChange (change);
            expect (waitUntil ([&] { return listener.scans.load() >= 2; }), "no scan after the notification");
            engine.stop();
            engine.removeListener (&listener);
        }

        beginTest ("The worker retries soon while enumeration fails (boot)");
        {
            MockEnumerator en;
            en.result = result::fail;
            MockExclusiveStore ex;
            MockFormatStore fm;
            WatchdogEngine engine (en, ex, fm, config(), fastOptions());
            expect (engine.start());
            expect (waitUntil ([&] { return en.calls.load() >= 3; }, 3000));
            engine.stop();
        }

        beginTest ("Format standardization is off by default");
        {
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            en.endpoints = { endpoint ("r1") };
            fm.current["r1"] = fmt (44100, 16, 16);
            fm.supported = { { 48000, 24, 24, false } };
            WatchdogEngine engine (en, ex, fm, config());
            engine.scanOnce();
            expectEquals (fm.writeCalls.load(), 0);
        }

        beginTest ("The two features are independent");
        {
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            en.endpoints = { endpoint ("r1") };
            ex.allow["r1"] = true;
            fm.current["r1"] = fmt (44100, 16, 16);
            fm.supported = { { 48000, 24, 24, false } };

            auto cfg = config();
            cfg.exclusiveModeProtection = false;
            cfg.formatStandardization = true;
            {
                WatchdogEngine engine (en, ex, fm, cfg);
                const auto r = engine.scanOnce();
                expectEquals (r.formatApplied, 1);
                expectEquals (ex.writeCalls.load(), 0);
                expect (ex.allow["r1"]);
            }
            cfg.exclusiveModeProtection = true;
            {
                WatchdogEngine engine (en, ex, fm, cfg);
                const auto r = engine.scanOnce();
                expectEquals (r.exclusiveFixed, 1);
                expectEquals (r.formatCompliant, 1);
            }
        }

        beginTest ("Format standardization skips inactive endpoints");
        {
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            en.endpoints = { endpoint ("r1", EndpointFlow::render, EndpointState::unplugged) };
            fm.current["r1"] = fmt (44100, 16, 16);
            fm.supported = { { 48000, 24, 24, false } };
            auto cfg = config();
            cfg.formatStandardization = true;
            WatchdogEngine engine (en, ex, fm, cfg);
            engine.scanOnce();
            expectEquals (fm.writeCalls.load(), 0);
        }

        beginTest ("Triggered scans back off after a failure; full scans retry");
        {
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            en.endpoints = { endpoint ("r1") };
            ex.allow["r1"] = true;
            ex.writesStick = false; // stubborn driver
            WatchdogEngine engine (en, ex, fm, config());
            expectEquals (engine.scanOnce (ScanKind::full).exclusiveFailed, 1);
            expectEquals (engine.scanOnce (ScanKind::triggered).exclusiveSkipped, 1);
            expectEquals (engine.scanOnce (ScanKind::full).exclusiveFailed, 1);
            expectEquals (ex.writeCalls.load(), 2);
        }

        beginTest ("A re-created endpoint is retried immediately");
        {
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            en.endpoints = { endpoint ("r1") };
            ex.allow["r1"] = true;
            ex.writesStick = false;
            WatchdogEngine engine (en, ex, fm, config());
            engine.scanOnce (ScanKind::full);
            DeviceChange removed;
            removed.kind = DeviceChange::Kind::removed;
            removed.endpointId = "r1";
            engine.onDeviceChange (removed);
            DeviceChange added;
            added.kind = DeviceChange::Kind::added;
            added.endpointId = "r1";
            engine.onDeviceChange (added);
            ex.writesStick = true; // the re-created endpoint accepts the change
            expectEquals (engine.scanOnce (ScanKind::triggered).exclusiveFixed, 1);
        }

        beginTest ("Status snapshot");
        {
            MockEnumerator en;
            MockExclusiveStore ex;
            MockFormatStore fm;
            auto render = endpoint ("r1");
            render.name = "Speakers";
            render.isDefault = true;
            en.endpoints = { render, endpoint ("c1", EndpointFlow::capture, EndpointState::unplugged) };
            ex.allow["r1"] = true;
            fm.current["r1"] = fmt (48000, 24, 24);
            WatchdogEngine engine (en, ex, fm, config());
            expect (! engine.getStatus().hasScanned);
            engine.scanOnce();
            const auto s = engine.getStatus();
            expect (s.hasScanned);
            expectEquals (static_cast<int> (s.totalScans), 1);
            expectEquals (static_cast<int> (s.totalExclusiveFixes), 1);
            expectEquals (static_cast<int> (s.endpoints.size()), 2);
            expectEquals (s.endpoints[0].name, juce::String ("Speakers"));
            expectEquals (s.endpoints[0].exclusive, juce::String ("blocked"));
            expectEquals (s.endpoints[0].format, juce::String ("48000 Hz / 24-bit"));
            expect (s.endpoints[0].isDefault);
            expect (s.endpoints[1].format.isEmpty());
        }
    }
};

static WatchdogEngineTests watchdogEngineTests;
} // namespace audioslave::test
