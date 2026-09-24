#include "platform/windows/WinCommon.h"
#include "ipc/ControlClient.h"
#include "ipc/PipeServer.h"
#include "tests/Mocks.h"

namespace audioslave::test
{
class IpcTests final : public juce::UnitTest
{
public:
    IpcTests() : juce::UnitTest ("IPC channel", "IPC") {}

    // A server that answers STATUS / PAUSE and rejects anything else.
    static ipc::PipeServer::Handler handler (std::atomic<int>& requests)
    {
        return [&requests] (const juce::MemoryBlock& payload)
        {
            ++requests;
            const auto m = ipc::decode (payload);
            if (m.type != ipc::Message::Type::request)
                return ipc::encodeResponse (m.id, false, "expected a request", nullptr);
            const auto command = ipc::parseCommand (m.command);
            if (! command || (*command != ipc::Command::status && *command != ipc::Command::pause))
                return ipc::encodeResponse (m.id, false, "unknown command: " + m.command, nullptr);
            ipc::StatusSnapshot s;
            s.pid = 42;
            s.state = *command == ipc::Command::pause ? EngineState::paused : EngineState::running;
            return ipc::encodeResponse (m.id, true, {}, &s);
        };
    }

    struct Listener : ipc::ControlClient::Listener
    {
        std::atomic<int> connected { 0 }, disconnected { 0 }, pushed { 0 };
        void controlConnected() override { ++connected; }
        void controlDisconnected() override { ++disconnected; }
        void statusPushed (const ipc::StatusSnapshot&) override { ++pushed; }
    };

    // Sends one raw frame with juce::NamedPipe and returns the decoded answer.
    static ipc::Message rawExchange (const juce::String& pipeName, const juce::MemoryBlock& framed)
    {
        juce::NamedPipe pipe;
        if (! pipe.openExisting (pipeName))
            return {};
        pipe.write (framed.getData(), static_cast<int> (framed.getSize()), 2000);
        juce::uint32 header[2] {};
        if (pipe.read (header, 8, 2000) != 8)
            return {};
        juce::MemoryBlock payload (header[1], true);
        if (pipe.read (payload.getData(), static_cast<int> (header[1]), 2000) != static_cast<int> (header[1]))
            return {};
        return ipc::decode (payload);
    }

    void runTest() override
    {
        beginTest ("Service unavailable: connect fails fast, requests fail cleanly");
        {
            ipc::ControlClient client (false, uniqueName ("Audioslave.Test"));
            const auto start = juce::Time::getMillisecondCounter();
            expect (! client.connect());
            expect (juce::Time::getMillisecondCounter() - start < 3000);
            const auto reply = client.request (ipc::Command::status, 1000);
            expect (! reply.delivered);
            expect (reply.error.isNotEmpty());
        }

        const auto pipeName = uniqueName ("Audioslave.Test");
        std::atomic<int> requests { 0 };
        ipc::PipeServer server (pipeName, ipc::PipeServer::portableSddl, handler (requests));
        juce::String error;
        expect (server.start (error), error);

        beginTest ("A second server cannot take the same pipe name");
        {
            std::atomic<int> unused { 0 };
            ipc::PipeServer intruder (pipeName, ipc::PipeServer::portableSddl, handler (unused));
            juce::String intruderError;
            expect (! intruder.start (intruderError));
            expect (intruderError.isNotEmpty());
        }

        beginTest ("Connection and commands");
        {
            Listener listener;
            ipc::ControlClient client (false, pipeName);
            client.setListener (&listener);
            expect (client.connect());
            expect (waitUntil ([&] { return listener.connected.load() == 1; }));

            auto reply = client.request (ipc::Command::status);
            expect (reply.delivered && reply.ok, reply.error);
            expect (reply.status.has_value() && reply.status->pid == 42);

            reply = client.request (ipc::Command::pause);
            expect (reply.ok);
            expect (reply.status->state == EngineState::paused);

            expect (waitUntil ([&] { return server.getNumConnections() == 1; }));
            client.setListener (nullptr);
            client.disconnect();
            expect (waitUntil ([&] { return server.getNumConnections() == 0; }), "server kept a closed connection");
        }

        beginTest ("Commands the server does not accept");
        {
            ipc::ControlClient client (false, pipeName);
            expect (client.connect());
            const auto reply = client.request (ipc::Command::reload);
            expect (reply.delivered);
            expect (! reply.ok);
            expect (reply.error.startsWith ("unknown command"));
        }

        beginTest ("Invalid messages: unknown command text, bad JSON, foreign protocol");
        {
            auto m = rawExchange (pipeName, ipc::frame (ipc::encodeRequest (9, "FORMAT C:")));
            expect (m.type == ipc::Message::Type::response && ! m.ok);
            expectEquals (m.id, 9);

            m = rawExchange (pipeName, ipc::frame (juce::MemoryBlock ("{oops", 5)));
            expect (m.type == ipc::Message::Type::response && ! m.ok);

            // A frame with a foreign magic number: the server drops the client.
            juce::NamedPipe pipe;
            expect (pipe.openExisting (pipeName));
            const juce::uint32 bogus[2] = { 0xdeadbeef, 4 };
            pipe.write (bogus, 8, 1000);
            char buffer[8] {};
            expect (pipe.read (buffer, 8, 1000) <= 0);
        }

        beginTest ("Status pushed to every connected client");
        {
            Listener a, b;
            ipc::ControlClient clientA (false, pipeName), clientB (false, pipeName);
            clientA.setListener (&a);
            clientB.setListener (&b);
            expect (clientA.connect() && clientB.connect());
            expect (waitUntil ([&] { return server.getNumConnections() == 2; }));
            ipc::StatusSnapshot s;
            server.broadcast (ipc::encodeStatusEvent (s));
            expect (waitUntil ([&] { return a.pushed.load() == 1 && b.pushed.load() == 1; }));
            clientA.setListener (nullptr);
            clientB.setListener (nullptr);
        }

        beginTest ("Server shutdown disconnects clients and fails pending requests");
        {
            Listener listener;
            ipc::ControlClient client (false, pipeName);
            client.setListener (&listener);
            expect (client.connect());
            expect (client.request (ipc::Command::status).ok);
            server.stop();
            expect (waitUntil ([&] { return listener.disconnected.load() == 1; }), "client never saw the disconnect");
            expect (! client.request (ipc::Command::status, 1000).delivered);
            client.setListener (nullptr);
        }
    }
};

static IpcTests ipcTests;
} // namespace audioslave::test
