#include "ipc/Protocol.h"

namespace audioslave::ipc
{
namespace
{
const juce::Identifier kProto ("proto"), kId ("id"), kCmd ("cmd"), kOk ("ok"), kError ("error"), kEvent ("event"),
    kStatus ("status"), kArgs ("args"), kResult ("result");

Compatibility compatibilityFromName (const juce::String& s)
{
    for (auto c : { Compatibility::compatible, Compatibility::rateUnsupported, Compatibility::depthUnsupported })
        if (compatibilityName (c) == s)
            return c;
    return Compatibility::unknown;
}

EndpointFlow flowFromName (const juce::String& flow)
{
    return flow == "render" ? EndpointFlow::render : flow == "capture" ? EndpointFlow::capture : EndpointFlow::unknown;
}

EndpointState stateFromInt (int state)
{
    switch (state)
    {
        case 1:  return EndpointState::active;
        case 2:  return EndpointState::disabled;
        case 8:  return EndpointState::unplugged;
        default: return EndpointState::notPresent;
    }
}

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
    o->setProperty ("description", e.description);
    o->setProperty ("customName", e.customName);
    o->setProperty ("disabledByAudioslave", e.disabledByAudioslave);
    o->setProperty ("compatibility", compatibilityName (e.compatibility));
    o->setProperty ("capabilities", e.capabilities);
    o->setProperty ("action", deviceActionName (e.action));
    o->setProperty ("reason", e.reason);
    return juce::var (o);
}

juce::var toVar (const DeviceEvent& e)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("seq", e.sequence);
    o->setProperty ("time", e.timeMs);
    o->setProperty ("action", deviceActionName (e.action));
    o->setProperty ("id", e.id);
    o->setProperty ("name", e.name);
    o->setProperty ("reason", e.reason);
    o->setProperty ("interactive", e.interactive);
    return juce::var (o);
}

DeviceEvent eventFromVar (const juce::var& v)
{
    DeviceEvent e;
    e.sequence = static_cast<juce::int64> (v.getProperty ("seq", 0));
    e.timeMs = static_cast<juce::int64> (v.getProperty ("time", 0));
    e.action = deviceActionFromName (v.getProperty ("action", {}).toString());
    e.id = v.getProperty ("id", {}).toString();
    e.name = v.getProperty ("name", {}).toString();
    e.reason = v.getProperty ("reason", {}).toString();
    e.interactive = v.getProperty ("interactive", false);
    return e;
}

EndpointStatus endpointFromVar (const juce::var& v)
{
    EndpointStatus e;
    e.id = v.getProperty ("id", {}).toString();
    e.name = v.getProperty ("name", {}).toString();
    e.flow = flowFromName (v.getProperty ("flow", {}).toString());
    e.state = stateFromInt (v.getProperty ("state", 4));
    e.isDefault = v.getProperty ("default", false);
    e.exclusive = v.getProperty ("exclusive", {}).toString();
    e.format = v.getProperty ("format", {}).toString();
    e.description = v.getProperty ("description", {}).toString();
    e.customName = v.getProperty ("customName", false);
    e.disabledByAudioslave = v.getProperty ("disabledByAudioslave", false);
    e.compatibility = compatibilityFromName (v.getProperty ("compatibility", {}).toString());
    e.capabilities = v.getProperty ("capabilities", {}).toString();
    e.action = deviceActionFromName (v.getProperty ("action", {}).toString());
    e.reason = v.getProperty ("reason", {}).toString();
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
    o->setProperty ("devicesDisabled", r.devicesDisabled);
    o->setProperty ("devicesReenabled", r.devicesReenabled);
    o->setProperty ("namesRestored", r.namesRestored);
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
    r.devicesDisabled = v.getProperty ("devicesDisabled", 0);
    r.devicesReenabled = v.getProperty ("devicesReenabled", 0);
    r.namesRestored = v.getProperty ("namesRestored", 0);
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
        case Command::stop:      return "STOP";
        case Command::reload:    return "RELOAD";
        case Command::analyze:   return "ANALYZE";
        case Command::configure: return "CONFIGURE";
        case Command::rename:    return "RENAME";
        case Command::enable:    return "ENABLE";
    }
    return "STATUS";
}

std::optional<Command> parseCommand (const juce::String& text)
{
    const auto t = text.trim().toUpperCase();
    for (auto c : { Command::status, Command::pause, Command::resume, Command::scan, Command::stop, Command::reload,
                    Command::analyze, Command::configure, Command::rename, Command::enable })
        if (t == commandName (c))
            return c;
    return std::nullopt;
}

juce::var toVar (const AudioSettings& a)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("formatStandardization", a.formatStandardization);
    o->setProperty ("sampleRate", static_cast<int> (a.sampleRate));
    o->setProperty ("bitDepth", static_cast<int> (a.bitDepth));
    o->setProperty ("disableIncompatibleDevices", a.disableIncompatibleDevices);
    o->setProperty ("confirmDisable", a.confirmDisable);
    return juce::var (o);
}

AudioSettings audioSettingsFromVar (const juce::var& v)
{
    AudioSettings a;
    a.formatStandardization = v.getProperty ("formatStandardization", false);
    a.sampleRate = static_cast<std::uint32_t> (static_cast<int> (v.getProperty ("sampleRate", 48000)));
    a.bitDepth = static_cast<std::uint16_t> (static_cast<int> (v.getProperty ("bitDepth", 24)));
    a.disableIncompatibleDevices = v.getProperty ("disableIncompatibleDevices", false);
    a.confirmDisable = v.getProperty ("confirmDisable", false);
    return a;
}

juce::var toVar (const DeviceReport& d)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", d.id);
    o->setProperty ("name", d.name);
    o->setProperty ("flow", endpointFlowName (d.flow));
    o->setProperty ("state", static_cast<int> (d.state));
    o->setProperty ("default", d.isDefault);
    o->setProperty ("disabledByAudioslave", d.disabledByAudioslave);
    o->setProperty ("current", d.currentFormat);
    o->setProperty ("capabilities", d.capabilities.serialise());
    o->setProperty ("compatibility", compatibilityName (d.compatibility));
    o->setProperty ("action", deviceActionName (d.action));
    o->setProperty ("reason", d.reason);
    return juce::var (o);
}

DeviceReport deviceReportFromVar (const juce::var& v)
{
    DeviceReport d;
    d.id = v.getProperty ("id", {}).toString();
    d.name = v.getProperty ("name", {}).toString();
    d.flow = flowFromName (v.getProperty ("flow", {}).toString());
    d.state = stateFromInt (v.getProperty ("state", 4));
    d.isDefault = v.getProperty ("default", false);
    d.disabledByAudioslave = v.getProperty ("disabledByAudioslave", false);
    d.currentFormat = v.getProperty ("current", {}).toString();
    d.capabilities = FormatCapabilities::deserialise (v.getProperty ("capabilities", {}).toString());
    d.compatibility = compatibilityFromName (v.getProperty ("compatibility", {}).toString());
    d.action = deviceActionFromName (v.getProperty ("action", {}).toString());
    d.reason = v.getProperty ("reason", {}).toString();
    return d;
}

juce::var toVar (const std::vector<DeviceReport>& devices)
{
    juce::Array<juce::var> list;
    for (const auto& d : devices)
        list.add (toVar (d));
    return list;
}

std::vector<DeviceReport> deviceReportsFromVar (const juce::var& value)
{
    std::vector<DeviceReport> out;
    if (const auto* list = value.getArray())
        for (const auto& d : *list)
            out.push_back (deviceReportFromVar (d));
    return out;
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
    o->setProperty ("sampleRate", s.sampleRate);
    o->setProperty ("bitDepth", s.bitDepth);
    o->setProperty ("disableIncompatibleDevices", s.disableIncompatibleDevices);
    o->setProperty ("disablePolicyConfirmed", s.disablePolicyConfirmed);
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
    o->setProperty ("pendingDisable", toVar (s.pendingDisable));
    juce::Array<juce::var> events;
    for (const auto& e : s.events)
        events.add (toVar (e));
    o->setProperty ("events", events);
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
    s.sampleRate = v.getProperty ("sampleRate", 48000);
    s.bitDepth = v.getProperty ("bitDepth", 24);
    s.disableIncompatibleDevices = v.getProperty ("disableIncompatibleDevices", false);
    s.disablePolicyConfirmed = v.getProperty ("disablePolicyConfirmed", false);
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
    s.pendingDisable = deviceReportsFromVar (v.getProperty ("pendingDisable", {}));
    if (const auto* list = v.getProperty ("events", {}).getArray())
        for (const auto& e : *list)
            s.events.push_back (eventFromVar (e));
    s.logsDir = v.getProperty ("logsDir", {}).toString();
    s.connectedClients = v.getProperty ("clients", 0);
    return s;
}

juce::MemoryBlock encodeRequest (int id, const juce::String& command, const juce::var& args)
{
    auto v = baseObject();
    v.getDynamicObject()->setProperty (kId, id);
    v.getDynamicObject()->setProperty (kCmd, command);
    if (! args.isVoid())
        v.getDynamicObject()->setProperty (kArgs, args);
    return toBlock (v);
}

juce::MemoryBlock encodeResponse (int id, bool ok, const juce::String& error, const StatusSnapshot* status,
                                  const juce::var& result)
{
    auto v = baseObject();
    auto* o = v.getDynamicObject();
    o->setProperty (kId, id);
    o->setProperty (kOk, ok);
    if (error.isNotEmpty())
        o->setProperty (kError, error);
    if (status != nullptr)
        o->setProperty (kStatus, toVar (*status));
    if (! result.isVoid())
        o->setProperty (kResult, result);
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
        m.args = value.getProperty (kArgs, {});
    }
    else if (value.hasProperty (kOk))
    {
        m.type = Message::Type::response;
        m.ok = value.getProperty (kOk, false);
        m.error = value.getProperty (kError, {}).toString();
        m.result = value.getProperty (kResult, {});
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
