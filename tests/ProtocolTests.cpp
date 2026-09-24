#include "ipc/Protocol.h"
#include "tests/Mocks.h"

namespace audioslave::test
{
class ProtocolTests final : public juce::UnitTest
{
public:
    ProtocolTests() : juce::UnitTest ("IPC protocol", "IPC") {}

    static ipc::StatusSnapshot sampleStatus()
    {
        ipc::StatusSnapshot s;
        s.version = "1.0.0";
        s.pid = 1234;
        s.mode = "service";
        s.state = EngineState::paused;
        s.startedAt = juce::Time (1700000000000);
        s.formatStandardization = true;
        s.formatTarget = "48000 Hz / 24-bit";
        s.hasScanned = true;
        s.lastScanTime = juce::Time (1700000060000);
        s.lastScan.kind = ScanKind::triggered;
        s.lastScan.endpointsScanned = 3;
        s.lastScan.exclusiveFixed = 1;
        s.totalScans = 9;
        s.totalExclusiveFixes = 4;
        EndpointStatus e;
        e.id = "{0.0.0.00000000}.{abc}";
        e.name = juce::String (juce::CharPointer_UTF8 ("Alto-falantes (Áudio)"));
        e.flow = EndpointFlow::render;
        e.state = EndpointState::unplugged;
        e.isDefault = true;
        e.exclusive = "blocked";
        e.format = "48000 Hz / 24-bit";
        s.endpoints.push_back (e);
        s.logsDir = "C:\\Program Files\\Audioslave\\logs";
        return s;
    }

    void runTest() override
    {
        beginTest ("Command names");
        {
            for (auto c : { ipc::Command::status, ipc::Command::pause, ipc::Command::resume, ipc::Command::scan,
                            ipc::Command::stop, ipc::Command::reload })
                expect (ipc::parseCommand (ipc::commandName (c)) == c);
            expect (ipc::parseCommand (" pause ") == ipc::Command::pause);
            expect (! ipc::parseCommand ("REBOOT").has_value());
        }

        beginTest ("Request round trip");
        {
            const auto m = ipc::decode (ipc::encodeRequest (7, "SCAN"));
            expect (m.type == ipc::Message::Type::request);
            expectEquals (m.id, 7);
            expectEquals (m.command, juce::String ("SCAN"));
        }

        beginTest ("Response with status round trip");
        {
            const auto in = sampleStatus();
            const auto m = ipc::decode (ipc::encodeResponse (3, true, {}, &in));
            expect (m.type == ipc::Message::Type::response);
            expect (m.ok);
            expectEquals (m.id, 3);
            expect (m.status.has_value());
            const auto& s = *m.status;
            expectEquals (s.pid, 1234);
            expect (s.state == EngineState::paused);
            expect (s.startedAt == in.startedAt);
            expect (s.lastScan.kind == ScanKind::triggered);
            expectEquals (s.lastScan.exclusiveFixed, 1);
            expectEquals (static_cast<int> (s.totalExclusiveFixes), 4);
            expectEquals (static_cast<int> (s.endpoints.size()), 1);
            expectEquals (s.endpoints[0].name, in.endpoints[0].name);
            expect (s.endpoints[0].state == EndpointState::unplugged);
            expect (s.endpoints[0].isDefault);
            expectEquals (s.logsDir, in.logsDir);
        }

        beginTest ("Error response and status event");
        {
            auto m = ipc::decode (ipc::encodeResponse (5, false, "unknown command", nullptr));
            expect (m.type == ipc::Message::Type::response);
            expect (! m.ok);
            expectEquals (m.error, juce::String ("unknown command"));

            m = ipc::decode (ipc::encodeStatusEvent (sampleStatus()));
            expect (m.type == ipc::Message::Type::event);
            expect (m.status.has_value());
        }

        beginTest ("Malformed input is rejected, never thrown");
        {
            const auto text = [] (const char* t) { return juce::MemoryBlock (t, std::strlen (t)); };
            expect (ipc::decode (juce::MemoryBlock()).type == ipc::Message::Type::invalid);
            expect (ipc::decode (text ("not json")).type == ipc::Message::Type::invalid);
            expect (ipc::decode (text ("[1,2,3]")).type == ipc::Message::Type::invalid);
            expect (ipc::decode (text ("{\"proto\":2,\"id\":1,\"cmd\":\"STATUS\"}")).type == ipc::Message::Type::invalid);
            expect (ipc::decode (text ("{\"proto\":1,\"id\":1}")).type == ipc::Message::Type::invalid);
            juce::MemoryBlock huge (static_cast<size_t> (ipc::maxMessageBytes) + 1, true);
            expect (ipc::decode (huge).type == ipc::Message::Type::invalid);
        }

        beginTest ("Frames match juce::InterprocessConnection (magic, size, little-endian)");
        {
            const juce::MemoryBlock payload ("abc", 3);
            const auto framed = ipc::frame (payload);
            expectEquals (static_cast<int> (framed.getSize()), 11);
            const auto* bytes = static_cast<const juce::uint8*> (framed.getData());
            expectEquals (static_cast<juce::int64> (juce::ByteOrder::littleEndianInt (bytes)), static_cast<juce::int64> (ipc::magic));
            expectEquals (static_cast<int> (juce::ByteOrder::littleEndianInt (bytes + 4)), 3);
            expect (std::memcmp (bytes + 8, "abc", 3) == 0);
        }
    }
};

static ProtocolTests protocolTests;
} // namespace audioslave::test
