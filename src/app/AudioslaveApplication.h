#pragma once
// Audioslave.exe: the per-session system-tray application (JUCE).
//
// With the service installed the tray is only a client of the service's
// control pipe. Without it (portable use) it runs the same ServiceHost on a
// background thread of this process and still talks to it through the pipe,
// so there is one code path and the host can never outlive the tray.
// Closing the tray (logoff, installer) never stops the service; "Encerrar"
// stops it cleanly (exit code 0, so the SCM recovery actions do not restart
// it).

#include "app/Theme.h"
#include "app/TrayController.h"
#include "ipc/ControlClient.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace audioslave
{
namespace win
{
class SessionInstance;
} // namespace win

class PortableHost;

class TrayIcon;
class StatusWindow;

class AudioslaveApplication final : public juce::JUCEApplication,
                                    private ipc::ControlClient::Listener,
                                    private juce::Timer
{
public:
    AudioslaveApplication();
    ~AudioslaveApplication() override;

    const juce::String getApplicationName() override;
    const juce::String getApplicationVersion() override;
    // One tray per *session* is enforced by win::SessionInstance (JUCE's own
    // check is machine-wide).
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override;
    void shutdown() override;
    void systemRequestedQuit() override;
    void anotherInstanceStarted (const juce::String&) override {}

private:
    // ControlClient::Listener (message thread)
    void controlConnected() override;
    void controlDisconnected() override;
    void statusPushed (const ipc::StatusSnapshot& status) override;

    void timerCallback() override;

    void refresh();
    void showMenu (juce::Rectangle<int> iconArea);
    void showStatusWindow();
    void showSettings();
    // Every status received (pushed or in a reply): device notifications and
    // the pending-confirmation prompt.
    void statusReceived (const ipc::StatusSnapshot& status);
    void notifyDeviceEvents (const ipc::StatusSnapshot& status);
    void promptPendingDisable (const ipc::StatusSnapshot& status);
    void request (ipc::Command command, const juce::var& args, std::function<void (const ipc::Reply&)> done);
    void pauseMonitoring();
    void resumeMonitoring();
    void scanNow();
    void confirmExit();
    void exitAndStop();
    void openLogs();
    void startPortableHost();
    void startServiceInBackground (bool reportErrors);
    void sendCommand (ipc::Command command, const juce::String& failureText, std::function<void (bool)> done = {});
    void showError (const juce::String& message);

    // Runs `job` on the background pool, then `done` on the message thread
    // (skipped if the application is shutting down).
    void runInBackground (std::function<juce::String()> job, std::function<void (const juce::String& error)> done);

    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>> (true);
    theme::LookAndFeel lookAndFeel_;
    std::unique_ptr<win::SessionInstance> session_;
    std::unique_ptr<ipc::ControlClient> client_;
    std::unique_ptr<TrayIcon> tray_;
    std::unique_ptr<StatusWindow> window_;
    std::unique_ptr<PortableHost> portableHost_;
    std::unique_ptr<juce::TooltipWindow> tooltips_;
    juce::ThreadPool pool_ { juce::ThreadPoolOptions().withThreadName ("Audioslave tray job").withNumberOfThreads (2) };
    TrayController controller_;
    std::atomic<bool> connecting_ { false };
    bool serviceMode_ = false;
    bool quitting_ = false;
    juce::int64 lastEventSequence_ = -1; // -1: nothing seen yet (never replay old events)
    bool pendingPromptShown_ = false;
    int portableRestarts_ = 0;
};
} // namespace audioslave
