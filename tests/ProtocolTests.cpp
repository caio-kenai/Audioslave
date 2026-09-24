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
                            ipc::Command::stop, ipc::Command::reload, ipc::Command::analyze, ipc::Command::configure,
                            ipc::Command::rename, ipc::Command::enable })
                expect (ipc::parseCommand (ipc::commandName (c)) == c);
            expect (ipc::parseCommand (" pause ") == ipc::Command::pause);
            expect (! ipc::parseCommand ("REBOOT").has_value());
        }

        beginTest ("Request with arguments and response with a result");
        {
            ipc::AudioSettings a;
            a.formatStandardization = true;
            a.sampleRate = 384000;
            a.bitDepth = 32;
            a.disableIncompatibleDevices = true;
            a.confirmDisable = true;
            auto m = ipc::decode (ipc::encodeRequest (8, "CONFIGURE", ipc::toVar (a)));
            expect (m.type == ipc::Message::Type::request);
            expect (ipc::audioSettingsFromVar (m.args) == a);

            DeviceReport d;
            d.id = "{0.0.1.00000000}.{cam}";
            d.name = "Webcam";
            d.flow = EndpointFlow::capture;
            d.state = EndpointState::disabled;
            d.disabledByAudioslave = true;
            d.currentFormat = "48000 Hz / 16-bit";
            d.capabilities = FormatCapabilities::deserialise ("44100:16;48000:16");
            d.compatibility = Compatibility::depthUnsupported;
            d.action = DeviceAction::disabled;
            d.reason = "requested bit depth is not supported";
            auto* o = new juce::DynamicObject();
            o->setProperty ("devices", ipc::toVar (std::vector<DeviceReport> { d }));
            m = ipc::decode (ipc::encodeResponse (8, true, {}, nullptr, juce::var (o)));
            const auto back = ipc::deviceReportsFromVar (m.result.getProperty ("devices", {}));
            expectEquals (static_cast<int> (back.size()), 1);
            expectEquals (back[0].id, d.id);
            expect (back[0].flow == EndpointFlow::capture);
            expect (back[0].state == EndpointState::disabled);
            expect (back[0].disabledByAudioslave);
            expect (back[0].capabilities == d.capabilities);
            expect (back[0].compatibility == Compatibility::depthUnsupported);
            expect (back[0].action == DeviceAction::disabled);
            expectEquals (back[0].reason, d.reason);
        }

        beginTest ("Status carries the audio settings, pending devices and events");
        {
            auto s = sampleStatus();
            s.sampleRate = 44100;
            s.bitDepth = 16;
            s.disableIncompatibleDevices = true;
            s.disablePolicyConfirmed = true;
            DeviceReport p;
            p.id = "x";
            p.name = "HDMI";
            p.action = DeviceAction::pending;
            s.pendingDisable.push_back (p);
            DeviceEvent e;
            e.sequence = 42;
            e.action = DeviceAction::disabled;
            e.name = "HDMI";
            e.interactive = true;
            s.events.push_back (e);
            s.endpoints[0].customName = true;
            s.endpoints[0].description = "Alto-falantes";
            s.endpoints[0].compatibility = Compatibility::rateUnsupported;
            s.endpoints[0].action = DeviceAction::keepDisabled;
            const auto back = ipc::statusFromVar (ipc::toVar (s));
            expectEquals (back.sampleRate, 44100);
            expectEquals (back.bitDepth, 16);
            expect (back.disableIncompatibleDevices && back.disablePolicyConfirmed);
            expectEquals (static_cast<int> (back.pendingDisable.size()), 1);
            expect (back.pendingDisable[0].action == DeviceAction::pending);
            expectEquals (static_cast<int> (back.events.size()), 1);
            expect (back.events[0].sequence == 42 && back.events[0].interactive);
            expect (back.endpoints[0].customName);
            expectEquals (back.endpoints[0].description, juce::String ("Alto-falantes"));
            expect (back.endpoints[0].compatibility == Compatibility::rateUnsupported);
            expect (back.endpoints[0].action == DeviceAction::keepDisabled);
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
