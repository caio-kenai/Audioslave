#include "ipc/ControlClient.h"
#include "common/Branding.h"

namespace audioslave::ipc
{
namespace
{
constexpr int pipeReadTimeoutMs = -1; // the reader blocks until data or disconnect
}

ControlClient::ControlClient (bool callbacksOnMessageThread, juce::String pipeName)
    : juce::InterprocessConnection (callbacksOnMessageThread, magic),
      onMessageThread_ (callbacksOnMessageThread),
      pipeName_ (pipeName.isNotEmpty() ? pipeName : juce::String (brand::controlPipeName))
{
}

ControlClient::~ControlClient()
{
    listener_ = nullptr;
    disconnect();
    failAllPending ("client destroyed");
}

bool ControlClient::connect()
{
    return connectToPipe (pipeName_, pipeReadTimeoutMs);
}

void ControlClient::disconnect()
{
    juce::InterprocessConnection::disconnect();
}

bool ControlClient::isConnected() const
{
    return juce::InterprocessConnection::isConnected();
}

void ControlClient::deliver (std::function<void (const Reply&)> callback, const Reply& reply)
{
    if (! callback)
        return;
    if (onMessageThread_ && ! juce::MessageManager::getInstance()->isThisTheMessageThread())
        juce::MessageManager::callAsync ([callback = std::move (callback), reply] { callback (reply); });
    else
        callback (reply);
}

void ControlClient::send (Command command, std::function<void (const Reply&)> onReply)
{
    send (command, juce::var(), std::move (onReply));
}

void ControlClient::send (Command command, const juce::var& args, std::function<void (const Reply&)> onReply)
{
    int id = 0;
    {
        const juce::ScopedLock sl (pendingLock_);
        id = nextId_++;
        pending_[id] = std::move (onReply);
    }
    if (! isConnected() || ! sendMessage (encodeRequest (id, commandName (command), args)))
    {
        std::function<void (const Reply&)> callback;
        {
            const juce::ScopedLock sl (pendingLock_);
            if (auto it = pending_.find (id); it != pending_.end())
            {
                callback = std::move (it->second);
                pending_.erase (it);
            }
        }
        Reply reply;
        reply.error = "not connected to the Audioslave service";
        deliver (std::move (callback), reply);
    }
}

Reply ControlClient::request (Command command, int timeoutMs)
{
    return request (command, juce::var(), timeoutMs);
}

Reply ControlClient::request (Command command, const juce::var& args, int timeoutMs)
{
    // A blocking wait on the message thread would dead-lock GUI clients.
    jassert (! onMessageThread_);

    auto done = std::make_shared<juce::WaitableEvent>();
    auto result = std::make_shared<Reply>();
    send (command, args, [done, result] (const Reply& r)
    {
        *result = r;
        done->signal();
    });
    if (! done->wait (timeoutMs))
    {
        Reply timedOut;
        timedOut.error = "timed out waiting for the Audioslave service";
        return timedOut;
    }
    return *result;
}

void ControlClient::connectionMade()
{
    if (auto* l = listener_)
        l->controlConnected();
}

void ControlClient::connectionLost()
{
    failAllPending ("the connection to the Audioslave service was lost");
    if (auto* l = listener_)
        l->controlDisconnected();
}

void ControlClient::failAllPending (const juce::String& reason)
{
    std::map<int, std::function<void (const Reply&)>> failed;
    {
        const juce::ScopedLock sl (pendingLock_);
        failed.swap (pending_);
    }
    Reply reply;
    reply.error = reason;
    for (auto& [id, callback] : failed)
        deliver (std::move (callback), reply);
}

void ControlClient::messageReceived (const juce::MemoryBlock& message)
{
    const auto decoded = decode (message);
    if (decoded.type == Message::Type::event && decoded.status)
    {
        if (auto* l = listener_)
            l->statusPushed (*decoded.status);
        return;
    }
    if (decoded.type != Message::Type::response)
        return;

    std::function<void (const Reply&)> callback;
    {
        const juce::ScopedLock sl (pendingLock_);
        if (auto it = pending_.find (decoded.id); it != pending_.end())
        {
            callback = std::move (it->second);
            pending_.erase (it);
        }
    }
    Reply reply;
    reply.delivered = true;
    reply.ok = decoded.ok;
    reply.error = decoded.error;
    reply.status = decoded.status;
    reply.result = decoded.result;
    deliver (std::move (callback), reply);
}
} // namespace audioslave::ipc
