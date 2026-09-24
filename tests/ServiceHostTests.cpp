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
        juce::File configFile = tempFile ("host-config.ini");
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
