#include "core/DeviceStateStore.h"
#include "logging/Logger.h"

#include <algorithm>

namespace audioslave
{
namespace
{
Compatibility compatibilityFromName (const juce::String& s)
{
    for (auto c : { Compatibility::compatible, Compatibility::rateUnsupported, Compatibility::depthUnsupported })
        if (compatibilityName (c) == s)
            return c;
    return Compatibility::unknown;
}

EndpointFlow flowFromName (const juce::String& s)
{
    return s == "render" ? EndpointFlow::render : s == "capture" ? EndpointFlow::capture : EndpointFlow::unknown;
}
} // namespace

DeviceStateStore::DeviceStateStore (juce::File file) : file_ (std::move (file))
{
    load();
}

std::optional<DisabledDeviceRecord> DeviceStateStore::get (const juce::String& endpointId) const
{
    const juce::ScopedLock sl (lock_);
    for (const auto& r : records_)
        if (r.id.equalsIgnoreCase (endpointId))
            return r;
    return std::nullopt;
}

std::vector<DisabledDeviceRecord> DeviceStateStore::all() const
{
    const juce::ScopedLock sl (lock_);
    return records_;
}

void DeviceStateStore::put (const DisabledDeviceRecord& record)
{
    const juce::ScopedLock sl (lock_);
    auto it = std::find_if (records_.begin(), records_.end(), [&] (const auto& r) { return r.id.equalsIgnoreCase (record.id); });
    if (it != records_.end())
    {
        if (*it == record)
            return;
        *it = record;
    }
    else
    {
        records_.push_back (record);
    }
    save();
}

void DeviceStateStore::remove (const juce::String& endpointId)
{
    const juce::ScopedLock sl (lock_);
    const auto before = records_.size();
    records_.erase (std::remove_if (records_.begin(), records_.end(), [&] (const auto& r) { return r.id.equalsIgnoreCase (endpointId); }),
                    records_.end());
    if (records_.size() != before)
        save();
}

void DeviceStateStore::resetHistory()
{
    const juce::ScopedLock sl (lock_);
    bool changed = false;
    for (auto& r : records_)
    {
        if (r.leftEnabled || ! r.history.empty())
        {
            r.leftEnabled = false;
            r.history.clear();
            changed = true;
        }
    }
    // Records of devices that are no longer disabled have served their purpose.
    const auto before = records_.size();
    records_.erase (std::remove_if (records_.begin(), records_.end(), [] (const auto& r) { return ! r.disabled; }), records_.end());
    if (changed || records_.size() != before)
        save();
}

void DeviceStateStore::load()
{
    if (file_ == juce::File() || ! file_.existsAsFile())
        return;
    const auto parsed = juce::JSON::parse (file_.loadFileAsString());
    const auto* list = parsed.getProperty ("devices", {}).getArray();
    if (list == nullptr)
    {
        Logger::instance().warn ("Device state file " + file_.getFullPathName() + " is not valid; starting empty.");
        return;
    }
    for (const auto& v : *list)
    {
        DisabledDeviceRecord r;
        r.id = v.getProperty ("id", {}).toString();
        if (r.id.isEmpty())
            continue;
        r.name = v.getProperty ("name", {}).toString();
        r.flow = flowFromName (v.getProperty ("flow", {}).toString());
        r.disabledAtMs = static_cast<juce::int64> (v.getProperty ("disabledAt", 0));
        r.requested = v.getProperty ("requested", {}).toString();
        r.reason = v.getProperty ("reason", {}).toString();
        r.compatibility = compatibilityFromName (v.getProperty ("compatibility", {}).toString());
        r.capabilities = FormatCapabilities::deserialise (v.getProperty ("capabilities", {}).toString());
        if (const auto* h = v.getProperty ("history", {}).getArray())
            for (const auto& t : *h)
                r.history.push_back (static_cast<juce::int64> (t));
        r.leftEnabled = v.getProperty ("leftEnabled", false);
        r.disabled = v.getProperty ("disabled", true);
        records_.push_back (std::move (r));
    }
}

void DeviceStateStore::save() const
{
    if (file_ == juce::File())
        return;
    juce::Array<juce::var> list;
    for (const auto& r : records_)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", r.id);
        o->setProperty ("name", r.name);
        o->setProperty ("flow", endpointFlowName (r.flow));
        o->setProperty ("disabledAt", r.disabledAtMs);
        o->setProperty ("disabledAtText", juce::Time (r.disabledAtMs).toISO8601 (true));
        o->setProperty ("requested", r.requested);
        o->setProperty ("reason", r.reason);
        o->setProperty ("compatibility", compatibilityName (r.compatibility));
        o->setProperty ("capabilities", r.capabilities.serialise());
        juce::Array<juce::var> history;
        for (auto t : r.history)
            history.add (t);
        o->setProperty ("history", history);
        o->setProperty ("leftEnabled", r.leftEnabled);
        o->setProperty ("disabled", r.disabled);
        list.add (juce::var (o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("comment", "Audioslave: devices disabled by the incompatible-device policy. Written by the service.");
    root->setProperty ("devices", list);
    file_.getParentDirectory().createDirectory();
    if (! file_.replaceWithText (juce::JSON::toString (juce::var (root)), false, false, "\r\n"))
        Logger::instance().error ("Could not write the device state file " + file_.getFullPathName() + ".");
}
} // namespace audioslave
