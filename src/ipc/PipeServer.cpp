#include "platform/windows/WinCommon.h"
#include "ipc/PipeServer.h"
#include "ipc/Protocol.h"
#include "platform/windows/WinError.h"
#include "platform/windows/WinHandles.h"

#include <sddl.h>

#include <atomic>
#include <vector>

namespace audioslave::ipc
{
namespace
{
constexpr DWORD pipeBufferBytes = 64 * 1024;
constexpr DWORD writeTimeoutMs = 5000;
} // namespace

struct PipeServer::Impl final : private juce::Thread
{
    struct Connection final : private juce::Thread
    {
        Connection (Impl& ownerIn, win::UniqueHandle pipeIn)
            : juce::Thread ("Audioslave pipe client"), owner (ownerIn), pipe (std::move (pipeIn))
        {
            readEvent.reset (::CreateEventW (nullptr, TRUE, FALSE, nullptr));
            writeEvent.reset (::CreateEventW (nullptr, TRUE, FALSE, nullptr));
        }

        ~Connection() override
        {
            waitForThreadToExit (-1);
            ::DisconnectNamedPipe (pipe.get());
        }

        bool begin() { return readEvent && writeEvent && startThread(); }
        void join() { waitForThreadToExit (-1); }
        [[nodiscard]] bool isFinished() const noexcept { return finished.load(); }

        // Waits for an overlapped operation; false on stop, timeout or error.
        bool completeIo (OVERLAPPED& ov, DWORD timeoutMs, DWORD& transferred)
        {
            HANDLE handles[] = { owner.stopEvent.get(), ov.hEvent };
            const DWORD w = ::WaitForMultipleObjects (2, handles, FALSE, timeoutMs);
            if (w != WAIT_OBJECT_0 + 1)
            {
                ::CancelIoEx (pipe.get(), &ov);
                ::GetOverlappedResult (pipe.get(), &ov, &transferred, TRUE); // wait for the cancellation
                return false;
            }
            return ::GetOverlappedResult (pipe.get(), &ov, &transferred, FALSE) != FALSE;
        }

        bool readExact (void* destination, DWORD bytes)
        {
            auto* p = static_cast<char*> (destination);
            while (bytes > 0)
            {
                OVERLAPPED ov {};
                ov.hEvent = readEvent.get();
                ::ResetEvent (ov.hEvent);
                DWORD got = 0;
                if (! ::ReadFile (pipe.get(), p, bytes, &got, &ov))
                {
                    if (::GetLastError() != ERROR_IO_PENDING || ! completeIo (ov, INFINITE, got))
                        return false;
                }
                if (got == 0)
                    return false;
                p += got;
                bytes -= got;
            }
            return true;
        }

        bool send (const juce::MemoryBlock& framed)
        {
            const juce::ScopedLock sl (writeLock);
            if (finished.load())
                return false;
            auto* p = static_cast<const char*> (framed.getData());
            auto remaining = static_cast<DWORD> (framed.getSize());
            while (remaining > 0)
            {
                OVERLAPPED ov {};
                ov.hEvent = writeEvent.get();
                ::ResetEvent (ov.hEvent);
                DWORD written = 0;
                if (! ::WriteFile (pipe.get(), p, remaining, &written, &ov))
                {
                    if (::GetLastError() != ERROR_IO_PENDING || ! completeIo (ov, writeTimeoutMs, written))
                        return false;
                }
                if (written == 0)
                    return false;
                p += written;
                remaining -= written;
            }
            return true;
        }

        void run() override
        {
            for (;;)
            {
                juce::uint32 header[2] {};
                if (! readExact (header, sizeof (header)))
                    break;
                const auto magicIn = juce::ByteOrder::swapIfBigEndian (header[0]);
                const auto size = juce::ByteOrder::swapIfBigEndian (header[1]);
                if (magicIn != magic || size == 0 || size > static_cast<juce::uint32> (maxMessageBytes))
                    break; // not our protocol: drop the client

                juce::MemoryBlock payload (size, false);
                if (! readExact (payload.getData(), size))
                    break;

                juce::MemoryBlock response;
                try
                {
                    response = owner.handler (payload);
                }
                catch (...)
                {
                    response = encodeResponse (0, false, "internal error", nullptr);
                }
                if (! send (frame (response)))
                    break;
            }
            finished.store (true);
        }

        Impl& owner;
        win::UniqueHandle pipe, readEvent, writeEvent;
        juce::CriticalSection writeLock;
        std::atomic<bool> finished { false };
    };

    Impl (juce::String name, const wchar_t* sddlIn, Handler handlerIn)
        : juce::Thread ("Audioslave pipe server"),
          pipePath ("\\\\.\\pipe\\" + juce::File::createLegalFileName (name)),
          sddl (sddlIn),
          handler (std::move (handlerIn))
    {
        stopEvent.reset (::CreateEventW (nullptr, TRUE, FALSE, nullptr));
        connectEvent.reset (::CreateEventW (nullptr, TRUE, FALSE, nullptr));
    }

    ~Impl() override { stop(); }

    win::UniqueHandle createInstance (bool first, juce::String* error)
    {
        SECURITY_ATTRIBUTES sa {};
        sa.nLength = sizeof (sa);
        sa.lpSecurityDescriptor = securityDescriptor.get();
        sa.bInheritHandle = FALSE;

        const DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
        const DWORD pipeMode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
        win::UniqueHandle h (::CreateNamedPipeW (pipePath.toWideCharPointer(), openMode, pipeMode,
                                                 PIPE_UNLIMITED_INSTANCES, pipeBufferBytes, pipeBufferBytes, 0, &sa));
        if (! h && error != nullptr)
        {
            const DWORD code = ::GetLastError();
            *error = code == ERROR_ACCESS_DENIED
                         ? "the pipe " + pipePath + " already exists (another Audioslave host is running?)"
                         : "CreateNamedPipe(" + pipePath + "): " + win::win32ErrorText (code);
        }
        return h;
    }

    bool start (juce::String& error)
    {
        if (isThreadRunning())
            return true;
        if (! stopEvent || ! connectEvent)
        {
            error = "cannot create events: " + win::lastErrorText();
            return false;
        }
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (! ::ConvertStringSecurityDescriptorToSecurityDescriptorW (sddl, SDDL_REVISION_1, &sd, nullptr))
        {
            error = "invalid pipe security descriptor: " + win::lastErrorText();
            return false;
        }
        securityDescriptor.reset (sd);

        listening = createInstance (true, &error);
        if (! listening)
            return false;
        ::ResetEvent (stopEvent.get());
        if (! startThread())
        {
            listening.reset();
            error = "cannot start the pipe server thread";
            return false;
        }
        return true;
    }

    void stop()
    {
        if (stopEvent)
            ::SetEvent (stopEvent.get());
        waitForThreadToExit (-1);
        listening.reset();

        std::vector<std::unique_ptr<Connection>> closing;
        {
            const juce::ScopedLock sl (connectionsLock);
            closing.swap (connections);
        }
        for (auto& c : closing)
            c->join(); // every pending I/O waits on stopEvent
        closing.clear();
    }

    void reapFinishedLocked()
    {
        connections.erase (std::remove_if (connections.begin(), connections.end(),
                                           [] (const auto& c) { return c->isFinished(); }),
                           connections.end());
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            if (! listening)
            {
                listening = createInstance (false, nullptr);
                if (! listening)
                {
                    if (::WaitForSingleObject (stopEvent.get(), 1000) == WAIT_OBJECT_0)
                        return;
                    continue;
                }
            }

            OVERLAPPED ov {};
            ov.hEvent = connectEvent.get();
            ::ResetEvent (ov.hEvent);
            bool connected = false;
            if (::ConnectNamedPipe (listening.get(), &ov))
            {
                connected = true;
            }
            else
            {
                const DWORD err = ::GetLastError();
                if (err == ERROR_PIPE_CONNECTED)
                {
                    connected = true;
                }
                else if (err == ERROR_IO_PENDING)
                {
                    HANDLE handles[] = { stopEvent.get(), connectEvent.get() };
                    if (::WaitForMultipleObjects (2, handles, FALSE, INFINITE) != WAIT_OBJECT_0 + 1)
                    {
                        DWORD ignored = 0;
                        ::CancelIoEx (listening.get(), &ov);
                        ::GetOverlappedResult (listening.get(), &ov, &ignored, TRUE);
                        return;
                    }
                    DWORD ignored = 0;
                    connected = ::GetOverlappedResult (listening.get(), &ov, &ignored, FALSE) != FALSE;
                }
            }
            if (! connected)
            {
                listening.reset(); // create a fresh instance
                continue;
            }

            const juce::ScopedLock sl (connectionsLock);
            reapFinishedLocked();
            if (static_cast<int> (connections.size()) >= maxClients)
            {
                ::DisconnectNamedPipe (listening.get());
                listening.reset();
                continue;
            }
            auto connection = std::make_unique<Connection> (*this, std::move (listening));
            if (connection->begin())
                connections.push_back (std::move (connection));
        }
    }

    void broadcast (const juce::MemoryBlock& payload)
    {
        const auto framed = frame (payload);
        const juce::ScopedLock sl (connectionsLock);
        for (auto& c : connections)
            if (! c->isFinished())
                c->send (framed);
    }

    int numConnections() const
    {
        const juce::ScopedLock sl (connectionsLock);
        return static_cast<int> (std::count_if (connections.begin(), connections.end(),
                                                [] (const auto& c) { return ! c->isFinished(); }));
    }

    bool running() const { return isThreadRunning(); }

    const juce::String pipePath;
    const wchar_t* sddl;
    Handler handler;
    win::UniqueHandle stopEvent, connectEvent, listening;
    win::LocalMemory securityDescriptor;
    mutable juce::CriticalSection connectionsLock;
    std::vector<std::unique_ptr<Connection>> connections;
};

PipeServer::PipeServer (juce::String pipeName, const wchar_t* sddl, Handler handler)
    : impl_ (std::make_unique<Impl> (std::move (pipeName), sddl, std::move (handler)))
{
}

PipeServer::~PipeServer()
{
    stop();
}

bool PipeServer::start (juce::String& error) { return impl_->start (error); }
void PipeServer::stop() { impl_->stop(); }
void PipeServer::broadcast (const juce::MemoryBlock& payload) { impl_->broadcast (payload); }
int PipeServer::getNumConnections() const { return impl_->numConnections(); }
bool PipeServer::isRunning() const { return impl_->running(); }
} // namespace audioslave::ipc
