#pragma once
// Named-pipe server for the control channel (Windows native).
//
// juce::NamedPipe cannot be used on the server side: it creates the pipe with
// a NULL security descriptor, so a pipe created by the LocalSystem service is
// read-only for ordinary users (the tray could not send commands) and remote
// clients are not rejected. This server:
//   - applies an explicit DACL (SDDL),
//   - sets PIPE_REJECT_REMOTE_CLIENTS,
//   - creates the first instance with FILE_FLAG_FIRST_PIPE_INSTANCE (no squatting),
//   - serves several clients at once (one tray per session + CLI),
//   - uses overlapped I/O and a stop event, so stop() never hangs,
//   - frames messages exactly like juce::InterprocessConnection.

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

namespace audioslave::ipc
{
class PipeServer
{
public:
    // Handles one request payload and returns the response payload. Runs on
    // the connection's thread; may be called concurrently for several clients.
    using Handler = std::function<juce::MemoryBlock (const juce::MemoryBlock& request)>;

    // SDDL used by the service: SYSTEM and Administrators full control,
    // interactive users read/write.
    static constexpr const wchar_t* serviceSddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)";
    // Portable host (runs as the user): adds the owner.
    static constexpr const wchar_t* portableSddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)(A;;GRGW;;;IU)";

    static constexpr int maxClients = 16;

    PipeServer (juce::String pipeName, const wchar_t* sddl, Handler handler);
    ~PipeServer();

    PipeServer (const PipeServer&) = delete;
    PipeServer& operator= (const PipeServer&) = delete;

    // Creates the first pipe instance and starts accepting. False + error
    // when the pipe cannot be created (e.g. another process already owns it).
    bool start (juce::String& error);

    // Disconnects every client and joins all threads. Idempotent.
    void stop();

    // Sends `payload` (unframed) to every connected client.
    void broadcast (const juce::MemoryBlock& payload);

    [[nodiscard]] int getNumConnections() const;
    [[nodiscard]] bool isRunning() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace audioslave::ipc
