#include "platform/windows/WinCommon.h"
#include "ipc/ControlClient.h"
#include "ipc/PipeServer.h"
#include "service/ServiceHost.h"
#include "tests/Mocks.h"

#include <thread>

namespace audioslave::test
{
class ServiceHostTests final : public juce::UnitTest
{
public:
    ServiceHostTests() : juce::UnitTest ("Service host", "Service") {}

    struct Fixture
    {
        MockEnumerator enumerator;
        MockExclusiveStore exclusive;
        MockFormatStore format;
        MockEndpointAdmin admin { enumerator };
        juce::File configFile = tempFile ("host-config.ini");
        juce::File stateFile = tempFile ("host-devices.json");
        juce::String pipeName = uniqueName ("Audioslave.HostTest");
        juce::CriticalSection statesLock;
        juce::Array<EngineState> states;
        std::atomic<bool> startedCallback { false };
        std::unique_ptr<ServiceHost> host;
        std::thread thread;
        std::atomic<int> exitCode { -1 };

        Fixture()
        {
            enumerator.endpoints = { endpoint ("r1") };
            exclusive.allow["r1"] = true;
            Configuration cfg;
            cfg.checkIntervalSeconds = 3600;
            saveConfiguration (cfg, configFile);
        }

        ~Fixture()
        {
            if (host != nullptr)
                host->requestStop();
            if (thread.joinable())
                thread.join();
            configFile.deleteFile();
            stateFile.deleteFile();
        }

        void start()
        {
            ServiceHost::Options o;
            o.mode = ServiceHost::Mode::console;
            o.configFile = configFile;
            o.pipeName = pipeName;
            o.configureLogging = false;
            o.writeEventLog = false;
            o.watchDevices = false;
            o.enumerator = &enumerator;
            o.exclusiveStore = &exclusive;
            o.formatStore = &format;
            o.endpointAdmin = &admin;
            o.deviceStateFile = stateFile;
            o.onStateChanged = [this] (EngineState s)
            {
                const juce::ScopedLock sl (statesLock);
                states.add (s);
            };
            o.onStarted = [this] { startedCallback = true; };
            host = std::make_unique<ServiceHost> (o);
            thread = std::thread ([this] { exitCode = host->run(); });
        }

        bool sawState (EngineState s)
        {
            const juce::ScopedLock sl (statesLock);
            return states.contains (s);
        }
    };

    void runTest() override
    {
        beginTest ("Starts, protects, answers STATUS");
        {
            Fixture f;
            f.start();
            expect (waitUntil ([&] { return f.startedCallback.load(); }));
            expect (waitUntil ([&] { return f.exclusive.writeCalls.load() == 1; }), "the first scan did not run");

            ipc::ControlClient client (false, f.pipeName);
            expect (client.connect());
            const auto reply = client.request (ipc::Command::status);
            expect (reply.ok, reply.error);
            expect (reply.status->state == EngineState::running);
            expectEquals (reply.status->mode, juce::String ("console"));
            expect (waitUntil ([&]
                               {
                                   const auto r = client.request (ipc::Command::status);
                                   return r.status.has_value() && r.status->totalExclusiveFixes == 1;
                               }));
        }

        beginTest ("PAUSE / RESUME through the pipe; nothing is modified while paused");
        {
            Fixture f;
            f.start();
            expect (waitUntil ([&] { return f.exclusive.writeCalls.load() == 1; }));

            ipc::ControlClient client (false, f.pipeName);
            expect (client.connect());
            auto reply = client.request (ipc::Command::pause);
            expect (reply.ok);
            expect (reply.status->state == EngineState::paused);
            expect (f.sawState (EngineState::paused), "the SCM would not have been told");

            // Windows re-enables exclusive mode while paused: left alone.
            f.exclusive.allow["r1"] = true;
            reply = client.request (ipc::Command::scan);
            expect (! reply.ok);
            expectEquals (reply.error, juce::String ("monitoring is paused"));
            juce::Thread::sleep (300);
            expectEquals (f.exclusive.writeCalls.load(), 1);

            // Resume re-reads the configuration and runs a full scan.
            Configuration cfg;
            cfg.checkIntervalSeconds = 3600;
            cfg.formatStandardization = true;
            saveConfiguration (cfg, f.configFile);
            reply = client.request (ipc::Command::resume);
            expect (reply.ok);
            expect (reply.status->state == EngineState::running);
            expect (reply.status->formatStandardization);
            expect (f.sawState (EngineState::running));
            expect (waitUntil ([&] { return f.exclusive.writeCalls.load() == 2; }), "no full scan after resume");
        }

        beginTest ("SCM-style queued requests (pause / resume / reload)");
        {
            Fixture f;
            f.start();
            expect (waitUntil ([&] { return f.startedCallback.load(); }));
            f.host->requestPause();
            expect (waitUntil ([&] { return f.host->snapshot().state == EngineState::paused; }));
            f.host->requestResume();
            expect (waitUntil ([&] { return f.host->snapshot().state == EngineState::running; }));

            Configuration cfg;
            cfg.checkIntervalSeconds = 120;
            saveConfiguration (cfg, f.configFile);
            f.host->requestReload();
            expect (waitUntil ([&] { return f.host->snapshot().checkIntervalSeconds == 120; }));
        }

        beginTest ("Settings from the window: analyse, apply (confirmed or not), rename, enable");
        {
            Fixture f;
            f.enumerator.endpoints = { endpoint ("r1"), endpoint ("cam", EndpointFlow::capture) };
            f.exclusive.allow["r1"] = false;
            f.format.current["r1"] = fmt (44100, 16, 16);
            f.format.current["cam"] = fmt (48000, 16, 16);
            f.format.supportedBy["r1"] = { { 44100, 16, 16, false }, { 48000, 24, 24, false } };
            f.format.supportedBy["cam"] = { { 48000, 16, 16, false } };
            f.start();
            expect (waitUntil ([&] { return f.startedCallback.load(); }));
            ipc::ControlClient client (false, f.pipeName);
            expect (client.connect());

            ipc::AudioSettings s;
            s.formatStandardization = true;
            s.sampleRate = 48000;
            s.bitDepth = 24;
            s.disableIncompatibleDevices = true;

            // Preview: nothing changes.
            auto reply = client.request (ipc::Command::analyze, ipc::toVar (s));
            expect (reply.ok, reply.error);
            auto devices = ipc::deviceReportsFromVar (reply.result.getProperty ("devices", {}));
            expectEquals (static_cast<int> (devices.size()), 2);
            for (const auto& d : devices)
                expect (d.action == (d.id == "cam" ? DeviceAction::disable : DeviceAction::apply), d.id);
            expectEquals (f.format.writeCalls.load(), 0);
            expect (! loadConfiguration (f.configFile, false).config.formatStandardization);

            // Applied without the confirmation: saved, format applied, nothing disabled.
            reply = client.request (ipc::Command::configure, ipc::toVar (s));
            expect (reply.ok, reply.error);
            auto saved = loadConfiguration (f.configFile, false).config;
            expect (saved.formatStandardization && saved.disableIncompatibleDevices);
            expect (! disablePolicyConfirmed (saved));
            expectEquals (static_cast<int> (f.format.current["r1"].sampleRate), 48000);
            expectEquals (f.admin.disableCalls.load(), 0);
            expectEquals (static_cast<int> (reply.status->pendingDisable.size()), 1);
            devices = ipc::deviceReportsFromVar (reply.result.getProperty ("devices", {}));
            for (const auto& d : devices)
                if (d.id == "cam")
                    expect (d.action == DeviceAction::pending);

            // Confirmed: disabled, recorded, kept after a restart of the service.
            s.confirmDisable = true;
            reply = client.request (ipc::Command::configure, ipc::toVar (s));
            expect (reply.ok, reply.error);
            saved = loadConfiguration (f.configFile, false).config;
            expectEquals (saved.disableConfirmedFor, juce::String ("48000:24"));
            expectEquals (f.admin.disableCalls.load(), 1);
            expect (f.stateFile.existsAsFile());
            expect (DeviceStateStore (f.stateFile).get ("cam").has_value());
            expect (reply.status->pendingDisable.empty());

            // Rename: applied now and kept in the configuration.
            reply = client.request (ipc::Command::rename, juce::JSON::parse ("{\"id\":\"r1\",\"name\":\"Caixas\"}"));
            expect (reply.ok, reply.error);
            expectEquals (f.enumerator.endpoints[0].description, juce::String ("Caixas"));
            expectEquals (customDeviceName (loadConfiguration (f.configFile, false).config, "r1"), juce::String ("Caixas"));
            reply = client.request (ipc::Command::rename, juce::JSON::parse ("{\"id\":\"r1\",\"name\":\"\"}"));
            expect (reply.ok, reply.error);
            expect (customDeviceName (loadConfiguration (f.configFile, false).config, "r1").isEmpty());

            // The user enables the disabled device again.
            reply = client.request (ipc::Command::enable, juce::JSON::parse ("{\"id\":\"cam\"}"));
            expect (reply.ok, reply.error);
            expect (f.enumerator.endpoints[1].state == EndpointState::active);

            // Invalid values are refused and nothing is saved.
            s.sampleRate = 12345;
            reply = client.request (ipc::Command::configure, ipc::toVar (s));
            expect (! reply.ok);
            expectEquals (static_cast<int> (loadConfiguration (f.configFile, false).config.sampleRate), 48000);
        }

        beginTest ("Invalid commands are rejected");
        {
            Fixture f;
            f.start();
            expect (waitUntil ([&] { return f.startedCallback.load(); }));
            juce::NamedPipe pipe;
            expect (pipe.openExisting (f.pipeName));
            const auto framed = ipc::frame (ipc::encodeRequest (4, "SELF-DESTRUCT"));
            pipe.write (framed.getData(), static_cast<int> (framed.getSize()), 2000);
            juce::uint32 header[2] {};
            expectEquals (pipe.read (header, 8, 2000), 8);
            juce::MemoryBlock payload (header[1], true);
            pipe.read (payload.getData(), static_cast<int> (header[1]), 2000);
            const auto m = ipc::decode (payload);
            expect (m.type == ipc::Message::Type::response);
            expect (! m.ok);
            expect (m.error.startsWith ("unknown command"));
        }

        beginTest ("STOP through the pipe ends run() cleanly (exit code 0)");
        {
            Fixture f;
            f.start();
            expect (waitUntil ([&] { return f.startedCallback.load(); }));
            ipc::ControlClient client (false, f.pipeName);
            expect (client.connect());
            expect (client.request (ipc::Command::stop).ok);
            f.thread.join();
            expectEquals (f.exitCode.load(), ServiceHost::exitOk);
            expect (f.sawState (EngineState::stopped));
            expect (! client.request (ipc::Command::status, 1000).delivered);
        }

        beginTest ("Protection keeps running when the control pipe is unavailable");
        {
            Fixture f;
            std::atomic<int> unused { 0 };
            ipc::PipeServer squatter (f.pipeName, ipc::PipeServer::portableSddl,
                                      [&unused] (const juce::MemoryBlock&) { ++unused; return juce::MemoryBlock(); });
            juce::String error;
            expect (squatter.start (error));
            f.start();
            expect (waitUntil ([&] { return f.exclusive.writeCalls.load() == 1; }), "no protection without the pipe");
            expectEquals (f.host->snapshot().connectedClients, 0);
            squatter.stop();
        }
    }
};

static ServiceHostTests serviceHostTests;
} // namespace audioslave::test
