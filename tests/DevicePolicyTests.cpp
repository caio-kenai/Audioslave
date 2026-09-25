#include "core/WatchdogEngine.h"
#include "tests/Mocks.h"

#include <thread>

namespace audioslave::test
{
// The incompatible-device policy and the kept device names (WatchdogEngine
// with in-memory doubles; nothing real is touched).
class DevicePolicyTests final : public juce::UnitTest
{
public:
    DevicePolicyTests() : juce::UnitTest ("Device policy", "Core") {}

    using Layout = MockFormatStore::Layout;

    // Three devices, target 48000 / 24:
    //   "full"  48000 at 16 and 24-bit  -> compatible
    //   "cam"   48000 at 16-bit only    -> rate ok, bit depth limited
    //   "hdmi"  44100 only              -> rate unsupported
    struct Rig
    {
        MockEnumerator en;
        MockExclusiveStore ex;
        MockFormatStore fm;
        MockEndpointAdmin admin { en };
        DeviceStateStore state;
        juce::int64 wallMs = 1'700'000'000'000;
        std::unique_ptr<WatchdogEngine> engine;

        explicit Rig (Configuration cfg)
        {
            en.endpoints = { endpoint ("full"), endpoint ("cam", EndpointFlow::capture), endpoint ("hdmi") };
            for (const auto* id : { "full", "cam", "hdmi" })
                fm.current[id] = fmt (44100, 16, 16);
            fm.supportedBy["full"] = { { 48000, 16, 16, false }, { 48000, 24, 24, false } };
            fm.supportedBy["cam"] = { { 44100, 16, 16, false }, { 48000, 16, 16, false } };
            fm.supportedBy["hdmi"] = { { 44100, 16, 16, false }, { 44100, 24, 24, false } };

            WatchdogEngine::Options o;
            o.admin = &admin;
            o.deviceState = &state;
            o.wallClock = [this] { return wallMs; };
            engine = std::make_unique<WatchdogEngine> (en, ex, fm, cfg, o);
        }

        EndpointState stateOf (const juce::String& id) const
        {
            for (const auto& e : en.endpoints)
                if (e.id == id)
                    return e.state;
            return EndpointState::notPresent;
        }

        static const DeviceReport* find (const std::vector<DeviceReport>& list, const juce::String& id)
        {
            for (const auto& d : list)
                if (d.id == id)
                    return &d;
            return nullptr;
        }
    };

    static Configuration config (bool disable, bool confirmed)
    {
        Configuration cfg;
        cfg.checkIntervalSeconds = 3600;
        cfg.formatStandardization = true;
        cfg.sampleRate = 48000;
        cfg.bitDepth = 24;
        cfg.disableIncompatibleDevices = disable;
        if (confirmed)
            cfg.disableConfirmedFor = formatTargetKey (cfg);
        return cfg;
    }

    void runTest() override
    {
        beginTest ("Policy off: incompatible devices are ignored, compatible ones get the format");
        {
            Rig rig (config (false, false));
            const auto r = rig.engine->scanOnce();
            expectEquals (r.formatApplied, 1);
            expectEquals (r.formatUnsupported, 2);
            expectEquals (r.devicesDisabled, 0);
            expectEquals (rig.admin.disableCalls.load(), 0);
            expect (Rig::find (r.devices, "cam")->action == DeviceAction::ignore);
            expect (Rig::find (r.devices, "hdmi")->action == DeviceAction::ignore);
            expect (rig.stateOf ("cam") == EndpointState::active);
        }

        beginTest ("Bit-depth limit and missing sample rate are told apart");
        {
            Rig rig (config (false, false));
            const auto r = rig.engine->scanOnce();
            const auto* cam = Rig::find (r.devices, "cam");
            const auto* hdmi = Rig::find (r.devices, "hdmi");
            expect (cam->compatibility == Compatibility::depthUnsupported);
            expectEquals (static_cast<int> (cam->capabilities.maxDepthAt (48000)), 16);
            expect (cam->reason.contains ("bit depth"));
            expect (hdmi->compatibility == Compatibility::rateUnsupported);
            expect (hdmi->reason.contains ("sample rate"));
        }

        beginTest ("Policy on but not confirmed: nothing is disabled, the devices are pending");
        {
            Rig rig (config (true, false));
            const auto r = rig.engine->scanOnce();
            expectEquals (r.devicesDisabled, 0);
            expectEquals (rig.admin.disableCalls.load(), 0);
            const auto status = rig.engine->getStatus();
            expectEquals (static_cast<int> (status.pendingDisable.size()), 2);
            expect (Rig::find (r.devices, "cam")->action == DeviceAction::pending);
        }

        beginTest ("Confirmed for another format: still pending (a new target needs a new confirmation)");
        {
            auto cfg = config (true, false);
            cfg.disableConfirmedFor = "44100:16";
            Rig rig (cfg);
            rig.engine->scanOnce();
            expectEquals (rig.admin.disableCalls.load(), 0);
        }

        beginTest ("Confirmed: incompatible devices are disabled, verified, recorded and reported");
        {
            Rig rig (config (true, true));
            const auto r = rig.engine->scanOnce (ScanKind::full, true);
            expectEquals (r.devicesDisabled, 2);
            expect (rig.stateOf ("cam") == EndpointState::disabled);
            expect (rig.stateOf ("hdmi") == EndpointState::disabled);
            expect (rig.stateOf ("full") == EndpointState::active);
            const auto rec = rig.state.get ("cam");
            expect (rec.has_value() && rec->disabled);
            expect (rec->compatibility == Compatibility::depthUnsupported);
            expect (rec->capabilities.supports (48000, 16));
            expectEquals (rec->requested, juce::String ("48000 Hz / 24-bit"));
            const auto status = rig.engine->getStatus();
            expectEquals (static_cast<int> (status.events.size()), 2);
            expect (status.events[0].action == DeviceAction::disabled);
            expect (status.events[0].interactive);
            expect (status.pendingDisable.empty());
        }

        beginTest ("A disabled device stays disabled while still incompatible (no churn)");
        {
            Rig rig (config (true, true));
            rig.engine->scanOnce();
            const int disables = rig.admin.disableCalls.load();
            for (int i = 0; i < 5; ++i)
                rig.engine->scanOnce();
            expectEquals (rig.admin.disableCalls.load(), disables);
            expectEquals (rig.admin.enableCalls.load(), 0);
            // Listed as disabled by Audioslave, for the window.
            int listed = 0;
            for (const auto& e : rig.engine->getStatus().endpoints)
                if (e.disabledByAudioslave)
                    ++listed;
            expectEquals (listed, 2);
        }

        beginTest ("Turning the policy off enables the devices Audioslave disabled");
        {
            Rig rig (config (true, true));
            rig.engine->scanOnce();
            rig.engine->setConfig (config (false, false));
            const auto r = rig.engine->scanOnce();
            expectEquals (r.devicesReenabled, 2);
            expect (rig.stateOf ("cam") == EndpointState::active);
            expect (! rig.state.get ("cam").has_value());
        }

        beginTest ("Choosing a format the device supports enables it again");
        {
            Rig rig (config (true, true));
            rig.engine->scanOnce();
            auto cfg = config (true, false);
            cfg.bitDepth = 16; // 48000 / 16: "cam" is fine now, "hdmi" still lacks 48000 Hz
            cfg.disableConfirmedFor = formatTargetKey (cfg);
            rig.engine->setConfig (cfg);
            const auto r = rig.engine->scanOnce();
            expectEquals (r.devicesReenabled, 1);
            expect (rig.stateOf ("cam") == EndpointState::active);
            expect (rig.stateOf ("hdmi") == EndpointState::disabled);
        }

        beginTest ("Devices disabled by the user are never touched");
        {
            Rig rig (config (true, true));
            rig.en.endpoints.push_back (endpoint ("user-off", EndpointFlow::render, EndpointState::disabled));
            rig.engine->scanOnce();
            rig.engine->setConfig (config (false, false));
            rig.engine->scanOnce();
            expect (rig.stateOf ("user-off") == EndpointState::disabled);
            for (const auto& e : rig.engine->getStatus().endpoints)
                expect (e.id != "user-off");
        }

        beginTest ("No disable loop: cooldown, then at most 3 times a day, then left enabled");
        {
            Rig rig (config (true, true));
            rig.engine->scanOnce(); // 1st time
            expectEquals (rig.admin.disableCalls.load(), 2);

            // Windows re-creates it enabled right away: not disabled again during the cooldown.
            rig.admin.reappear ("cam");
            rig.wallMs += 1000;
            rig.engine->scanOnce();
            expect (rig.stateOf ("cam") == EndpointState::active);
            expectEquals (rig.admin.disableCalls.load(), 2);

            // After the cooldown: 2nd and 3rd time.
            for (int i = 0; i < 2; ++i)
            {
                rig.wallMs += WatchdogEngine::reapplyCooldownMs + 1000;
                rig.engine->scanOnce();
                expect (rig.stateOf ("cam") == EndpointState::disabled);
                rig.admin.reappear ("cam");
            }
            expectEquals (rig.admin.disableCalls.load(), 4);

            // A 4th time within 24 h: left enabled, reported once.
            rig.wallMs += WatchdogEngine::reapplyCooldownMs + 1000;
            const auto r = rig.engine->scanOnce();
            expect (rig.stateOf ("cam") == EndpointState::active);
            expect (Rig::find (r.devices, "cam")->action == DeviceAction::leftEnabled);
            for (int i = 0; i < 3; ++i)
            {
                rig.wallMs += WatchdogEngine::reapplyCooldownMs + 1000;
                rig.engine->scanOnce();
            }
            expectEquals (rig.admin.disableCalls.load(), 4);
            int leftEvents = 0;
            for (const auto& e : rig.engine->getStatus().events)
                leftEvents += e.action == DeviceAction::leftEnabled ? 1 : 0;
            expectEquals (leftEvents, 1);

            // A new decision by the user (settings changed) gives it a new chance.
            rig.engine->setConfig (config (false, false));
            rig.engine->setConfig (config (true, true));
            rig.engine->scanOnce();
            expect (rig.stateOf ("cam") == EndpointState::disabled);
        }

        beginTest ("A stale enumeration never takes a device Audioslave disabled for enabled again");
        {
            Rig rig (config (true, true));
            auto before = rig.en.endpoints; // enumerated while everything was still active
            rig.engine->scanOnce();
            expect (rig.stateOf ("cam") == EndpointState::disabled);
            rig.en.staleOnce = before;
            rig.engine->scanOnce();
            const auto rec = rig.state.get ("cam");
            expect (rec.has_value() && rec->disabled, "the record lost its disabled flag");
            expect (rec->history.size() == 1);
            // Still Audioslave's: turning the policy off enables it again.
            rig.engine->setConfig (config (false, false));
            rig.engine->scanOnce();
            expect (rig.stateOf ("cam") == EndpointState::active);
        }

        beginTest ("Scans run one at a time (worker and an applied change)");
        {
            Rig rig (config (true, true));
            std::vector<std::thread> threads;
            for (int i = 0; i < 4; ++i)
                threads.emplace_back ([&]
                {
                    for (int n = 0; n < 5; ++n)
                        rig.engine->scanOnce (ScanKind::full, true);
                });
            for (auto& t : threads)
                t.join();
            expectEquals (rig.admin.disableCalls.load(), 2); // each incompatible device exactly once
            expect (rig.state.get ("cam")->disabled);
            expect (rig.state.get ("hdmi")->disabled);
        }

        beginTest ("A disable that does not stick is a failure, not a success");
        {
            Rig rig (config (true, true));
            rig.admin.changesStick = false;
            const auto r = rig.engine->scanOnce();
            expectEquals (r.devicesDisabled, 0);
            expect (Rig::find (r.devices, "cam")->action == DeviceAction::failed);
            expect (! rig.state.get ("cam").has_value());
        }

        beginTest ("Unknown capabilities are never a reason to disable");
        {
            Rig rig (config (true, true));
            rig.fm.supportResult = static_cast<ResultCode> (0x80004002); // E_NOINTERFACE
            rig.engine->scanOnce();
            expectEquals (rig.admin.disableCalls.load(), 0);
        }

        beginTest ("Report only (Enforce=false): nothing is disabled");
        {
            auto cfg = config (true, true);
            cfg.enforce = false;
            Rig rig (cfg);
            rig.engine->scanOnce();
            expectEquals (rig.admin.disableCalls.load(), 0);
        }

        beginTest ("Analysis previews every action without touching anything");
        {
            Rig rig (config (false, false));
            auto candidate = config (true, false);
            auto list = rig.engine->analyze (candidate);
            expectEquals (static_cast<int> (list.size()), 3);
            expect (Rig::find (list, "full")->action == DeviceAction::apply);
            expect (Rig::find (list, "cam")->action == DeviceAction::disable);
            expect (Rig::find (list, "cam")->compatibility == Compatibility::depthUnsupported);
            expect (Rig::find (list, "hdmi")->action == DeviceAction::disable);
            candidate.disableIncompatibleDevices = false;
            list = rig.engine->analyze (candidate);
            expect (Rig::find (list, "cam")->action == DeviceAction::ignore);
            expectEquals (rig.admin.disableCalls.load(), 0);
            expectEquals (rig.fm.writeCalls.load(), 0);

            // Devices disabled by Audioslave are analysed from the formats saved before.
            Rig rig2 (config (true, true));
            rig2.engine->scanOnce();
            auto relaxed = config (true, true);
            relaxed.bitDepth = 16;
            list = rig2.engine->analyze (relaxed);
            expect (Rig::find (list, "cam")->action == DeviceAction::reenable);
            expect (Rig::find (list, "hdmi")->action == DeviceAction::keepDisabled);
        }

        beginTest ("Capabilities are asked once, again after a device change or a manual check");
        {
            Rig rig (config (false, false));
            rig.engine->scanOnce();
            rig.engine->analyze (config (false, false)); // first time every device is asked
            const int calls = rig.fm.supportCalls.load();
            rig.engine->scanOnce();
            rig.engine->analyze (config (false, false));
            const int perPass = rig.fm.supportCalls.load() - calls;
            // The second pass only checks the target (judgeAndApply), never the whole matrix again.
            expect (perPass < 20, juce::String (perPass));
            rig.engine->forgetCapabilities();
            rig.engine->analyze (config (false, false));
            expect (rig.fm.supportCalls.load() - calls - perPass > 40);
        }

        beginTest ("The user re-enables a device from the window: it stays enabled");
        {
            Rig rig (config (true, true));
            rig.engine->scanOnce();
            expect (rig.engine->enableDevice ("cam").isEmpty());
            expect (rig.stateOf ("cam") == EndpointState::active);
            rig.wallMs += WatchdogEngine::reapplyCooldownMs * 10;
            rig.engine->scanOnce();
            expect (rig.stateOf ("cam") == EndpointState::active);
        }

        beginTest ("Kept names are restored after Windows or a driver resets them, once");
        {
            auto cfg = config (false, false);
            cfg.deviceNames["full"] = juce::String::fromUTF8 ("Monitor Estúdio");
            Rig rig (cfg);
            rig.en.endpoints[0].description = "Speakers";
            auto r = rig.engine->scanOnce();
            expectEquals (r.namesRestored, 1);
            expectEquals (rig.en.endpoints[0].description, juce::String::fromUTF8 ("Monitor Estúdio"));
            r = rig.engine->scanOnce();
            expectEquals (r.namesRestored, 0);
            expectEquals (rig.admin.renameCalls.load(), 1);
            // A driver update puts the old name back.
            rig.en.endpoints[0].description = "Speakers";
            r = rig.engine->scanOnce();
            expectEquals (r.namesRestored, 1);
        }

        beginTest ("Rename from the window applies and verifies the name");
        {
            Rig rig (config (false, false));
            rig.en.endpoints[0].description = "Speakers";
            expect (rig.engine->renameDevice ("full", "  Caixas  ").isEmpty());
            expectEquals (rig.en.endpoints[0].description, juce::String ("Caixas"));
            expect (rig.engine->renameDevice ("full", "   ").isNotEmpty());
        }
    }
};

static DevicePolicyTests devicePolicyTests;
} // namespace audioslave::test
