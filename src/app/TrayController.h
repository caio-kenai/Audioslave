#pragma once
// What the tray shows and which actions it offers, as a pure function of
// (connection to the host, last status, SCM state, operation in flight).
// Kept free of GUI code so it is unit-tested.

#include "ipc/Protocol.h"
#include "service/ServiceController.h"

#include <optional>

namespace audioslave
{
class TrayController
{
public:
    enum class UiState
    {
        running,
        paused,
        stopped,
        starting,
        stopping,
        notInstalled,
        unknown
    };

    // How "Retomar monitoramento" is carried out.
    enum class ResumeAction
    {
        none,
        resumeThroughHost,   // IPC RESUME
        startService         // SCM start (the service was stopped)
    };

    struct MenuModel
    {
        juce::String title;
        juce::String statusLine;   // "Status: Em execução"
        bool pauseEnabled = false;
        bool resumeEnabled = false;
        bool scanEnabled = false;
        bool exitEnabled = true;
    };

    // Inputs -------------------------------------------------------------------
    void setPortable (bool portable) noexcept { portable_ = portable; }
    void setConnected (bool connected);
    void setStatus (const ipc::StatusSnapshot& status);
    void setServiceState (scm::State state) noexcept { serviceState_ = state; }
    void setBusy (bool busy) noexcept { busy_ = busy; }

    // Outputs ------------------------------------------------------------------
    [[nodiscard]] UiState uiState() const noexcept;
    [[nodiscard]] bool showPausedIcon() const noexcept;
    [[nodiscard]] MenuModel menu() const;
    [[nodiscard]] ResumeAction resumeAction() const noexcept;
    [[nodiscard]] bool isConnected() const noexcept { return connected_; }
    [[nodiscard]] bool isBusy() const noexcept { return busy_; }
    [[nodiscard]] bool isPortable() const noexcept { return portable_; }
    [[nodiscard]] const std::optional<ipc::StatusSnapshot>& status() const noexcept { return status_; }

    static juce::String uiStateText (UiState state);      // Portuguese, user-facing

private:
    bool portable_ = false;
    bool connected_ = false;
    bool busy_ = false;
    scm::State serviceState_ = scm::State::unknown;
    std::optional<ipc::StatusSnapshot> status_;
};
} // namespace audioslave
