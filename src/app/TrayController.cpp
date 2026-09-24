#include "app/TrayController.h"
#include "common/Branding.h"
#include "common/Strings.h"

namespace audioslave
{
juce::String TrayController::uiStateText (UiState state)
{
    switch (state)
    {
        case UiState::running:      return utf8 ("Em execução");
        case UiState::paused:       return "Pausado";
        case UiState::stopped:      return "Parado";
        case UiState::starting:     return utf8 ("Iniciando…");
        case UiState::stopping:     return utf8 ("Parando…");
        case UiState::notInstalled: return utf8 ("Serviço não instalado");
        case UiState::unknown:      break;
    }
    return "Desconhecido";
}

void TrayController::setConnected (bool connected)
{
    connected_ = connected;
    if (! connected)
        status_.reset();
}

void TrayController::setStatus (const ipc::StatusSnapshot& status)
{
    status_ = status;
}

TrayController::UiState TrayController::uiState() const noexcept
{
    if (connected_ && status_)
    {
        switch (status_->state)
        {
            case EngineState::running:  return UiState::running;
            case EngineState::paused:   return UiState::paused;
            case EngineState::stopping: return UiState::stopping;
            case EngineState::stopped:  return UiState::stopped;
        }
    }
    if (connected_)
        return UiState::starting; // connected, first status not received yet

    if (portable_)
        return UiState::starting; // the portable host is (re)starting

    switch (serviceState_)
    {
        case scm::State::notInstalled:    return UiState::notInstalled;
        case scm::State::stopped:         return UiState::stopped;
        case scm::State::startPending:
        case scm::State::continuePending:
        case scm::State::running:         return UiState::starting; // running but the pipe is not up yet
        case scm::State::paused:          return UiState::paused;
        case scm::State::stopPending:
        case scm::State::pausePending:    return UiState::stopping;
        case scm::State::unknown:         break;
    }
    return UiState::unknown;
}

bool TrayController::showPausedIcon() const noexcept
{
    const auto s = uiState();
    return s != UiState::running && s != UiState::starting;
}

TrayController::ResumeAction TrayController::resumeAction() const noexcept
{
    if (busy_)
        return ResumeAction::none;
    const auto s = uiState();
    if (s == UiState::paused && connected_)
        return ResumeAction::resumeThroughHost;
    if (s == UiState::stopped && ! portable_)
        return ResumeAction::startService;
    return ResumeAction::none;
}

TrayController::MenuModel TrayController::menu() const
{
    MenuModel m;
    m.title = brand::productName;
    const auto s = uiState();
    m.statusLine = "Status: " + uiStateText (s);
    m.pauseEnabled = ! busy_ && connected_ && s == UiState::running;
    m.resumeEnabled = resumeAction() != ResumeAction::none;
    m.scanEnabled = ! busy_ && connected_ && s == UiState::running;
    m.exitEnabled = ! busy_;
    return m;
}
} // namespace audioslave
