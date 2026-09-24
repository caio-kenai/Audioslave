#pragma once
// Control protocol between the tray / CLI and the service.
//
// Transport: \\.\pipe\Audioslave.Control, byte mode. Each message is framed
// exactly like juce::InterprocessConnection frames it - uint32 magic, uint32
// payload size (little-endian), payload - so the clients use
// juce::InterprocessConnection unchanged and the native server speaks the
// same wire format.
//
// Payload: UTF-8 JSON (juce::JSON), protocol version 1.
//   request   {"proto":1,"id":7,"cmd":"STATUS|PAUSE|RESUME|SCAN|STOP|RELOAD"}
//             {"proto":1,"id":7,"cmd":"ANALYZE|CONFIGURE|RENAME|ENABLE","args":{...}}
//   response  {"proto":1,"id":7,"ok":true,"status":{...},"result":{...}}
//             {"proto":1,"id":7,"ok":false,"error":"..."}
//   event     {"proto":1,"event":"status","status":{...}}
// New fields are optional and ignored by older peers.
//
//   ANALYZE   args: AudioSettings          result: {"devices":[DeviceReport...]}
//             preview of what the settings would do; nothing is changed.
//   CONFIGURE args: AudioSettings          result: {"devices":[...],"paused":bool}
//             saves the settings, reloads them and applies them at once.
//   RENAME    args: {"id","name"}          empty name: forget the kept name.
//   ENABLE    args: {"id"}                 re-enable a device Audioslave disabled.

#include "core/EngineStatus.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace audioslave::ipc
{
inline constexpr juce::uint32 magic = 0x41534C56; // "VLSA" little-endian
inline constexpr int protocolVersion = 1;
inline constexpr int maxMessageBytes = 256 * 1024;

enum class Command
{
    status,
    pause,
    resume,
    scan,
    stop,
    reload,
    analyze,
    configure,
    rename,
    enable
};

// Audio settings chosen in the window / the installer (CONFIGURE, ANALYZE).
struct AudioSettings
{
    bool formatStandardization = false;
    std::uint32_t sampleRate = 48000;
    std::uint16_t bitDepth = 24;
    bool disableIncompatibleDevices = false;
    // The user saw the list of devices that will be disabled and agreed.
    bool confirmDisable = false;

    bool operator== (const AudioSettings&) const = default;
};

juce::var toVar (const AudioSettings& settings);
AudioSettings audioSettingsFromVar (const juce::var& value);

juce::var toVar (const DeviceReport& device);
DeviceReport deviceReportFromVar (const juce::var& value);
juce::var toVar (const std::vector<DeviceReport>& devices);
std::vector<DeviceReport> deviceReportsFromVar (const juce::var& value);

juce::String commandName (Command command);
std::optional<Command> parseCommand (const juce::String& text);

// Everything the tray shows and `status` prints.
struct StatusSnapshot
{
    juce::String version;
    int pid = 0;
    juce::String mode;           // "service" | "portable" | "console"
    EngineState state = EngineState::stopped;
    juce::Time startedAt;

    bool exclusiveProtection = true;
    bool formatStandardization = false;
    bool enforce = true;
    juce::String formatTarget;   // "48000 Hz / 24-bit"
    int sampleRate = 48000;
    int bitDepth = 24;
    bool disableIncompatibleDevices = false;
    bool disablePolicyConfirmed = false;
    int checkIntervalSeconds = 60;

    bool hasScanned = false;
    juce::Time lastScanTime;
    ScanReport lastScan;
    juce::int64 totalScans = 0;
    juce::int64 totalExclusiveFixes = 0;
    juce::int64 totalFormatChanges = 0;
    std::vector<EndpointStatus> endpoints;
    std::vector<DeviceReport> pendingDisable;
    std::vector<DeviceEvent> events;

    juce::String logsDir;
    int connectedClients = 0;
};

juce::var toVar (const StatusSnapshot& status);
StatusSnapshot statusFromVar (const juce::var& value);

struct Message
{
    enum class Type
    {
        request,
        response,
        event,
        invalid
    };

    Type type = Type::invalid;
    int protocol = 0;
    int id = 0;
    juce::String command;        // request: raw command text
    juce::var args;              // request: command arguments (optional)
    bool ok = false;             // response
    juce::String error;          // response / invalid
    juce::var result;            // response: command result (optional)
    std::optional<StatusSnapshot> status;
};

juce::MemoryBlock encodeRequest (int id, const juce::String& command, const juce::var& args = {});
juce::MemoryBlock encodeResponse (int id, bool ok, const juce::String& error, const StatusSnapshot* status,
                                  const juce::var& result = {});
juce::MemoryBlock encodeStatusEvent (const StatusSnapshot& status);

// Never throws; malformed input yields Type::invalid with an error text.
Message decode (const juce::MemoryBlock& payload);

// Header + payload, as juce::InterprocessConnection writes it.
juce::MemoryBlock frame (const juce::MemoryBlock& payload);
} // namespace audioslave::ipc
