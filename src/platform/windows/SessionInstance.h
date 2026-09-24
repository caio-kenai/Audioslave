#pragma once
// One tray per Windows session (not per machine: juce::JUCEApplication's
// single-instance lock is a Global\ mutex, which would leave a second RDP
// session without a tray).
//
// The primary instance owns Local\Audioslave.Tray and two auto-reset events:
//   Show - a second launch in the same session asks it to open its window;
//   Quit - the installer asks it to close before replacing the executables.

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

namespace audioslave::win
{
class SessionInstance
{
public:
    SessionInstance();
    ~SessionInstance();

    SessionInstance (const SessionInstance&) = delete;
    SessionInstance& operator= (const SessionInstance&) = delete;

    // True when this process owns the session's tray.
    [[nodiscard]] bool isPrimary() const noexcept;

    // Primary only: invokes the callbacks from a background thread whenever
    // another process signals Show / Quit.
    void startListening (std::function<void()> onShow, std::function<void()> onQuit);
    void stopListening();

    // Ask the session's primary instance to show its window / to quit.
    // Return false when no tray runs in this session.
    static bool signalShow();
    static bool signalQuit();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace audioslave::win
