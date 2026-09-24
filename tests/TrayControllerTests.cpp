#include "app/TrayController.h"
#include "tests/Mocks.h"

namespace audioslave::test
{
class TrayControllerTests final : public juce::UnitTest
{
public:
    TrayControllerTests() : juce::UnitTest ("Tray controller", "Tray") {}

    static ipc::StatusSnapshot status (EngineState s)
    {
        ipc::StatusSnapshot snapshot;
        snapshot.state = s;
        return snapshot;
    }

    void runTest() override
    {
        using Ui = TrayController::UiState;
        using Resume = TrayController::ResumeAction;

        beginTest ("Startup: connected before the first status");
        {
            TrayController c;
            c.setConnected (true);
            expect (c.uiState() == Ui::starting);
            expect (! c.showPausedIcon());
            expect (! c.menu().pauseEnabled);
        }

        beginTest ("Running: Pausar enabled, Retomar disabled, normal icon");
        {
            TrayController c;
            c.setConnected (true);
            c.setStatus (status (EngineState::running));
            const auto m = c.menu();
            expect (c.uiState() == Ui::running);
            expect (m.pauseEnabled);
            expect (! m.resumeEnabled);
            expect (m.scanEnabled);
            expect (m.exitEnabled);
            expect (! c.showPausedIcon());
            expectEquals (m.statusLine, juce::String (juce::CharPointer_UTF8 ("Status: Em execução")));
            expectEquals (m.title, juce::String ("Audioslave"));
        }

        beginTest ("Paused: Retomar resumes through the host, grey icon");
        {
            TrayController c;
            c.setConnected (true);
            c.setStatus (status (EngineState::paused));
            expect (c.uiState() == Ui::paused);
            expect (! c.menu().pauseEnabled);
            expect (c.menu().resumeEnabled);
            expect (c.resumeAction() == Resume::resumeThroughHost);
            expect (c.showPausedIcon());
        }

        beginTest ("Service stopped (after Encerrar): Retomar starts the service");
        {
            TrayController c;
            c.setConnected (false);
            c.setServiceState (scm::State::stopped);
            expect (c.uiState() == Ui::stopped);
            expect (c.resumeAction() == Resume::startService);
            expect (c.menu().resumeEnabled);
            expect (! c.menu().pauseEnabled);
            expect (c.showPausedIcon());
        }

        beginTest ("Service unavailable: running in the SCM but no pipe yet");
        {
            TrayController c;
            c.setServiceState (scm::State::running);
            expect (c.uiState() == Ui::starting);
            expect (c.resumeAction() == Resume::none);
        }

        beginTest ("Losing the connection drops the stale status");
        {
            TrayController c;
            c.setConnected (true);
            c.setStatus (status (EngineState::running));
            c.setConnected (false);
            c.setServiceState (scm::State::stopPending);
            expect (! c.status().has_value());
            expect (c.uiState() == Ui::stopping);
        }

        beginTest ("An operation in flight disables every action");
        {
            TrayController c;
            c.setConnected (true);
            c.setStatus (status (EngineState::running));
            c.setBusy (true);
            const auto m = c.menu();
            expect (! m.pauseEnabled && ! m.resumeEnabled && ! m.scanEnabled && ! m.exitEnabled);
        }

        beginTest ("Portable mode never offers to start a service");
        {
            TrayController c;
            c.setPortable (true);
            c.setConnected (false);
            c.setServiceState (scm::State::notInstalled);
            expect (c.uiState() == Ui::starting);
            expect (c.resumeAction() == Resume::none);
        }

        beginTest ("Not installed");
        {
            TrayController c;
            c.setServiceState (scm::State::notInstalled);
            expect (c.uiState() == Ui::notInstalled);
            expectEquals (TrayController::uiStateText (Ui::notInstalled),
                          juce::String (juce::CharPointer_UTF8 ("Serviço não instalado")));
        }
    }
};

static TrayControllerTests trayControllerTests;
} // namespace audioslave::test
