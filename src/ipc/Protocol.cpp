#include "ipc/Protocol.h"

namespace audioslave::ipc
{
namespace
{
const juce::Identifier kProto ("proto"), kId ("id"), kCmd ("cmd"), kOk ("ok"), kError ("error"), kEvent ("event"),
    kStatus ("status");

juce::var toVar (const EndpointStatus& e)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", e.id);
    o->setProperty ("name", e.name);
    o->setProperty ("flow", endpointFlowName (e.flow));
    o->setProperty ("state", static_cast<int> (e.state));
    o->setProperty ("default", e.isDefault);
    o->setProperty ("exclusive", e.exclusive);
    o->setProperty ("format", e.format);
    return juce::var (o);
}

EndpointStatus endpointFromVar (const juce::var& v)
{
    EndpointStatus e;
    e.id = v.getProperty ("id", {}).toString();
    e.name = v.getProperty ("name", {}).toString();
    const auto flow = v.getProperty ("flow", {}).toString();
    e.flow = flow == "render" ? EndpointFlow::render : flow == "capture" ? EndpointFlow::capture : EndpointFlow::unknown;
    switch (static_cast<int> (v.getProperty ("state", 4)))
    {
        case 1:  e.state = EndpointState::active; break;
        case 2:  e.state = EndpointState::disabled; break;
        case 8:  e.state = EndpointState::unplugged; break;
        default: e.state = EndpointState::notPresent; break;
    }
    e.isDefault = v.getProperty ("default", false);
    e.exclusive = v.getProperty ("exclusive", {}).toString();
    e.format = v.getProperty ("format", {}).toString();
    return e;
}

juce::var toVar (const ScanReport& r)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("kind", r.kind == ScanKind::full ? "full" : "triggered");
    o->setProperty ("endpoints", r.endpointsScanned);
    o->setProperty ("exclusiveFixed", r.exclusiveFixed);
    o->setProperty ("exclusiveOk", r.exclusiveAlreadyOff);
    o->setProperty ("exclusiveSkipped", r.exclusiveSkipped);
    o->setProperty ("exclusiveFailed", r.exclusiveFailed);
    o->setProperty ("formatApplied", r.formatApplied);
    o->setProperty ("formatOk", r.formatCompliant);
    o->setProperty ("formatUnsupported", r.formatUnsupported);
    o->setProperty ("formatSkipped", r.formatSkipped);
    o->setProperty ("formatFailed", r.formatFailed);
    o->setProperty ("enumerationFailed", r.enumerationFailed);
    o->setProperty ("paused", r.paused);
    return juce::var (o);
}

ScanReport reportFromVar (const juce::var& v)
{
    ScanReport r;
    r.kind = v.getProperty ("kind", "full").toString() == "triggered" ? ScanKind::triggered : ScanKind::full;
    r.endpointsScanned = v.getProperty ("endpoints", 0);
    r.exclusiveFixed = v.getProperty ("exclusiveFixed", 0);
    r.exclusiveAlreadyOff = v.getProperty ("exclusiveOk", 0);
    r.exclusiveSkipped = v.getProperty ("exclusiveSkipped", 0);
    r.exclusiveFailed = v.getProperty ("exclusiveFailed", 0);
    r.formatApplied = v.getProperty ("formatApplied", 0);
    r.formatCompliant = v.getProperty ("formatOk", 0);
    r.formatUnsupported = v.getProperty ("formatUnsupported", 0);
    r.formatSkipped = v.getProperty ("formatSkipped", 0);
    r.formatFailed = v.getProperty ("formatFailed", 0);
    r.enumerationFailed = v.getProperty ("enumerationFailed", false);
    r.paused = v.getProperty ("paused", false);
    return r;
}

EngineState stateFromName (const juce::String& s)
{
    if (s == "RUNNING")
        return EngineState::running;
    if (s == "PAUSED")
        return EngineState::paused;
    if (s == "STOPPING")
        return EngineState::stopping;
    return EngineState::stopped;
}

juce::MemoryBlock toBlock (const juce::var& value)
{
    const auto text = juce::JSON::toString (value, juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::none));
    return juce::MemoryBlock (text.toRawUTF8(), text.getNumBytesAsUTF8());
}

juce::var baseObject()
{
    auto* o = new juce::DynamicObject();
    o->setProperty (kProto, protocolVersion);
    return juce::var (o);
}
} // namespace

juce::String commandName (Command command)
{
    switch (command)
    {
        case Command::status: return "STATUS";
        case Command::pause:  return "PAUSE";
        case Command::resume: return "RESUME";
        case Command::scan:   return "SCAN";
        case Command::stop:   return "STOP";
        case Command::reload: return "RELOAD";
    }
    return "STATUS";
}

std::optional<Command> parseCommand (const juce::String& text)
{
    const auto t = text.trim().toUpperCase();
    for (auto c : { Command::status, Command::pause, Command::resume, Command::scan, Command::stop, Command::reload })
        if (t == commandName (c))
            return c;
    return std::nullopt;
}

juce::var toVar (const StatusSnapshot& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("version", s.version);
    o->setProperty ("pid", s.pid);
    o->setProperty ("mode", s.mode);
    o->setProperty ("state", engineStateName (s.state));
    o->setProperty ("startedAt", s.startedAt.toMilliseconds());
    o->setProperty ("exclusiveProtection", s.exclusiveProtection);
    o->setProperty ("formatStandardization", s.formatStandardization);
    o->setProperty ("enforce", s.enforce);
    o->setProperty ("formatTarget", s.formatTarget);
    o->setProperty ("checkIntervalSeconds", s.checkIntervalSeconds);
    o->setProperty ("hasScanned", s.hasScanned);
    o->setProperty ("lastScanTime", s.lastScanTime.toMilliseconds());
    o->setProperty ("lastScan", toVar (s.lastScan));
    o->setProperty ("totalScans", s.totalScans);
    o->setProperty ("totalExclusiveFixes", s.totalExclusiveFixes);
    o->setProperty ("totalFormatChanges", s.totalFormatChanges);
    juce::Array<juce::var> endpoints;
    for (const auto& e : s.endpoints)
        endpoints.add (toVar (e));
    o->setProperty ("endpoints", endpoints);
    o->setProperty ("logsDir", s.logsDir);
    o->setProperty ("clients", s.connectedClients);
    return juce::var (o);
}

StatusSnapshot statusFromVar (const juce::var& v)
{
    StatusSnapshot s;
    s.version = v.getProperty ("version", {}).toString();
    s.pid = v.getProperty ("pid", 0);
    s.mode = v.getProperty ("mode", {}).toString();
    s.state = stateFromName (v.getProperty ("state", {}).toString());
    s.startedAt = juce::Time (static_cast<juce::int64> (v.getProperty ("startedAt", 0)));
    s.exclusiveProtection = v.getProperty ("exclusiveProtection", true);
    s.formatStandardization = v.getProperty ("formatStandardization", false);
    s.enforce = v.getProperty ("enforce", true);
    s.formatTarget = v.getProperty ("formatTarget", {}).toString();
    s.checkIntervalSeconds = v.getProperty ("checkIntervalSeconds", 60);
    s.hasScanned = v.getProperty ("hasScanned", false);
    s.lastScanTime = juce::Time (static_cast<juce::int64> (v.getProperty ("lastScanTime", 0)));
    s.lastScan = reportFromVar (v.getProperty ("lastScan", {}));
    s.totalScans = v.getProperty ("totalScans", 0);
    s.totalExclusiveFixes = v.getProperty ("totalExclusiveFixes", 0);
    s.totalFormatChanges = v.getProperty ("totalFormatChanges", 0);
    if (const auto* list = v.getProperty ("endpoints", {}).getArray())
        for (const auto& e : *list)
            s.endpoints.push_back (endpointFromVar (e));
    s.logsDir = v.getProperty ("logsDir", {}).toString();
    s.connectedClients = v.getProperty ("clients", 0);
    return s;
}

juce::MemoryBlock encodeRequest (int id, const juce::String& command)
{
    auto v = baseObject();
    v.getDynamicObject()->setProperty (kId, id);
    v.getDynamicObject()->setProperty (kCmd, command);
    return toBlock (v);
}

juce::MemoryBlock encodeResponse (int id, bool ok, const juce::String& error, const StatusSnapshot* status)
{
    auto v = baseObject();
    auto* o = v.getDynamicObject();
    o->setProperty (kId, id);
    o->setProperty (kOk, ok);
    if (error.isNotEmpty())
        o->setProperty (kError, error);
    if (status != nullptr)
        o->setProperty (kStatus, toVar (*status));
    return toBlock (v);
}

juce::MemoryBlock encodeStatusEvent (const StatusSnapshot& status)
{
    auto v = baseObject();
    v.getDynamicObject()->setProperty (kEvent, "status");
    v.getDynamicObject()->setProperty (kStatus, toVar (status));
    return toBlock (v);
}

Message decode (const juce::MemoryBlock& payload)
{
    Message m;
    if (payload.getSize() == 0 || payload.getSize() > static_cast<size_t> (maxMessageBytes))
    {
        m.error = "empty or oversized message";
        return m;
    }

    const auto text = juce::String::fromUTF8 (static_cast<const char*> (payload.getData()), static_cast<int> (payload.getSize()));
    juce::var value;
    if (const auto parsed = juce::JSON::parse (text, value); parsed.failed() || ! value.isObject())
    {
        m.error = "malformed JSON";
        return m;
    }

    m.protocol = value.getProperty (kProto, 0);
    if (m.protocol != protocolVersion)
    {
        m.error = "unsupported protocol version " + juce::String (m.protocol);
        return m;
    }
    m.id = value.getProperty (kId, 0);
    if (value.hasProperty (kStatus))
        m.status = statusFromVar (value.getProperty (kStatus, {}));

    if (value.hasProperty (kCmd))
    {
        m.type = Message::Type::request;
        m.command = value.getProperty (kCmd, {}).toString();
    }
    else if (value.hasProperty (kOk))
    {
        m.type = Message::Type::response;
        m.ok = value.getProperty (kOk, false);
        m.error = value.getProperty (kError, {}).toString();
    }
    else if (value.getProperty (kEvent, {}).toString() == "status" && m.status.has_value())
    {
        m.type = Message::Type::event;
    }
    else
    {
        m.error = "unknown message type";
    }
    return m;
}

juce::MemoryBlock frame (const juce::MemoryBlock& payload)
{
    juce::MemoryOutputStream out (payload.getSize() + 8);
    out.writeInt (static_cast<int> (magic));                  // little-endian
    out.writeInt (static_cast<int> (payload.getSize()));
    out.write (payload.getData(), payload.getSize());
    return out.getMemoryBlock();
}
} // namespace audioslave::ipc
