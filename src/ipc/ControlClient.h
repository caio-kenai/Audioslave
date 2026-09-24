#pragma once
// Client side of the control channel, built on juce::InterprocessConnection
// (reader thread, framing, connection-state callbacks). Used by the tray
// (callbacks on the JUCE message thread) and by the CLI (callbacks on the
// connection's own thread).

#include "ipc/Protocol.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <map>
#include <memory>

namespace audioslave::ipc
{
struct Reply
{
    bool delivered = false;  // false: not connected, timed out or connection lost
    bool ok = false;         // the service accepted the command
    juce::String error;
    std::optional<StatusSnapshot> status;
};

class ControlClient final : private juce::InterprocessConnection
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void controlConnected() {}
        virtual void controlDisconnected() {}
        virtual void statusPushed (const StatusSnapshot&) {}
    };

    // `callbacksOnMessageThread`: true for GUI code (requires a running JUCE
    // message loop), false for console code.
    explicit ControlClient (bool callbacksOnMessageThread, juce::String pipeName = {});
    ~ControlClient() override;

    // Opens the pipe (fails after ~200 ms when no host is listening).
    bool connect();
    void disconnect();
    [[nodiscard]] bool isConnected() const;

    // Asynchronous request: `onReply` runs on the callback thread (message
    // thread for GUI clients) exactly once, also when the connection drops.
    void send (Command command, std::function<void (const Reply&)> onReply);

    // Blocking request for console clients (callbacksOnMessageThread = false).
    Reply request (Command command, int timeoutMs = 10000);

    void setListener (Listener* listener) { listener_ = listener; }

private:
    void connectionMade() override;
    void connectionLost() override;
    void messageReceived (const juce::MemoryBlock& message) override;
    void failAllPending (const juce::String& reason);
    void deliver (std::function<void (const Reply&)> callback, const Reply& reply);

    const bool onMessageThread_;
    const juce::String pipeName_;
    Listener* listener_ = nullptr;

    juce::CriticalSection pendingLock_;
    std::map<int, std::function<void (const Reply&)>> pending_;
    int nextId_ = 1;
};
} // namespace audioslave::ipc
