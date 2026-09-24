#include "platform/windows/WinCommon.h"
#include "app/AudioslaveApplication.h"
#include "AudioslaveVersion.h"
#include "app/Dialog.h"
#include "app/StatusWindow.h"
#include "app/TrayIcon.h"
#include "app/TrayMenu.h"
#include "common/Branding.h"
#include "common/Strings.h"
#include "config/Configuration.h"
#include "logging/Logger.h"
#include "platform/windows/Paths.h"
#include "platform/windows/SessionInstance.h"
#include "service/ServiceController.h"
#include "service/ServiceHost.h"

namespace audioslave
{
namespace
{
constexpr int upkeepIntervalMs = 2000;
constexpr int maxPortableRestarts = 5;

} // namespace

// Portable mode: the monitoring host on a thread of the tray process.
class PortableHost final : private juce::Thread
{
public:
    PortableHost() : juce::Thread ("Audioslave portable host")
    {
        ServiceHost::Options options;
        options.mode = ServiceHost::Mode::portable;
        options.configureLogging = false; // the tray already logs to audioslave.log
        host_ = std::make_unique<ServiceHost> (options);
    }

    ~PortableHost() override { stop(); }

    bool start() { return startThread(); }
    [[nodiscard]] bool isRunning() const { return isThreadRunning(); }

    void stop()
    {
        host_->requestStop();
        waitForThreadToExit (-1); // the host always finishes its current pass
    }

private:
    void run() override { host_->run(); }

    std::unique_ptr<ServiceHost> host_;
};

namespace
{
void showAlert (juce::MessageBoxIconType icon, const juce::String& message, std::function<void (int)> callback = {})
{
    theme::showMessage (icon, brand::productName, message, [callback = std::move (callback)] { if (callback) callback (0); });
}

} // namespace

AudioslaveApplication::AudioslaveApplication() = default;
AudioslaveApplication::~AudioslaveApplication() = default;

const juce::String AudioslaveApplication::getApplicationName()
{
    return brand::productName;
}

const juce::String AudioslaveApplication::getApplicationVersion()
{
    return AUDIOSLAVE_VERSION_STRING;
}

void AudioslaveApplication::initialise (const juce::String&)
{
    // One tray per session: a second launch opens the first one's window.
    session_ = std::make_unique<win::SessionInstance>();
    if (! session_->isPrimary())
    {
        win::SessionInstance::signalShow();
        session_.reset();
        quit();
        return;
    }

    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel_);

    serviceMode_ = scm::exists();
    const auto config = loadConfiguration (paths::configFile(), false).config;
    auto& log = Logger::instance();
    paths::ensureDirectory (paths::logsDir());
    // The service owns audioslave.log; in portable mode the watchdog runs in
    // this process and writes it.
    const auto logFile = serviceMode_ ? paths::trayLogFile() : paths::serviceLogFile();
    log.configure (config.enableLogging ? logFile : juce::File(), config.logLevel, false);
    controller_.setPortable (! serviceMode_);
    log.info ("Audioslave " AUDIOSLAVE_VERSION_STRING " tray starting (pid " + juce::String (static_cast<juce::int64> (::GetCurrentProcessId()))
              + ", " + (serviceMode_ ? "service mode" : "portable mode") + ").");

    auto alive = alive_;
    session_->startListening (
        [this, alive]
        {
            juce::MessageManager::callAsync ([this, alive] { if (alive->load()) showStatusWindow(); });
        },
        [this, alive]
        {
            // The installer closes the icon; the service keeps running.
            juce::MessageManager::callAsync ([this, alive]
            {
                if (! alive->load())
                    return;
                Logger::instance().info ("Tray: close requested by the installer.");
                quit();
            });
        });

    client_ = std::make_unique<ipc::ControlClient> (true);
    client_->setListener (this);

    tray_ = std::make_unique<TrayIcon>();
    tray_->onOpen = [this] { showStatusWindow(); };
    tray_->onMenu = [this] { showMenu(); };

    if (serviceMode_)
    {
        const auto status = scm::query();
        controller_.setServiceState (status.state);
        // Opening the app brings the protection back if it was stopped.
        if (status.state == scm::State::stopped)
            startServiceInBackground (false);
    }
    else
    {
        startPortableHost();
    }

    refresh();
    timerCallback();
    startTimer (upkeepIntervalMs);
}

void AudioslaveApplication::shutdown()
{
    stopTimer();
    alive_->store (false);
    quitting_ = true;

    if (session_ != nullptr)
        session_->stopListening();
    window_.reset();
    tray_.reset();

    // Background jobs (a pipe connect may be in flight) finish first.
    pool_.removeAllJobs (true, 10000);

    if (client_ != nullptr)
    {
        client_->setListener (nullptr);
        client_->disconnect();
    }

    portableHost_.reset(); // portable mode: stops the in-process host cleanly

    client_.reset();
    session_.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);

    Logger::instance().info ("Audioslave tray stopped.");
    Logger::instance().close();
}

void AudioslaveApplication::systemRequestedQuit()
{
    // Logoff / shutdown: close the icon only; the service is left alone.
    quit();
}

//==============================================================================
void AudioslaveApplication::controlConnected()
{
    controller_.setConnected (true);
    Logger::instance().info ("Tray: connected to the Audioslave host.");
    sendCommand (ipc::Command::status, {});
    refresh();
}

void AudioslaveApplication::controlDisconnected()
{
    controller_.setConnected (false);
    Logger::instance().info ("Tray: disconnected from the Audioslave host.");
    refresh();
}

void AudioslaveApplication::statusPushed (const ipc::StatusSnapshot& status)
{
    controller_.setStatus (status);
    refresh();
}

void AudioslaveApplication::timerCallback()
{
    if (quitting_)
        return;

    if (serviceMode_)
    {
        const auto status = scm::query();
        if (status.state == scm::State::notInstalled)
        {
            // Uninstalled underneath us: nothing left to control.
            Logger::instance().info ("Tray: the service is no longer installed; closing the tray icon.");
            quit();
            return;
        }
        controller_.setServiceState (status.state);
    }
    else if (portableHost_ != nullptr && ! portableHost_->isRunning() && ! controller_.isBusy())
    {
        if (portableRestarts_ < maxPortableRestarts)
        {
            ++portableRestarts_;
            Logger::instance().warn ("Tray: the portable host exited; restarting it (" + juce::String (portableRestarts_) + ").");
            startPortableHost();
        }
    }

    if (client_ != nullptr && ! client_->isConnected() && ! connecting_.exchange (true))
    {
        // Opening the pipe may take up to ~200 ms: keep it off the message thread.
        auto* client = client_.get();
        runInBackground ([client]
                         {
                             client->connect();
                             return juce::String();
                         },
                         [this] (const juce::String&) { connecting_ = false; });
    }
    refresh();
}

void AudioslaveApplication::refresh()
{
    if (tray_ != nullptr)
        tray_->setPaused (controller_.showPausedIcon());
    if (window_ != nullptr)
        window_->update (controller_);
}

//==============================================================================
void AudioslaveApplication::showMenu()
{
    if (quitting_)
        return;
    auto menu = buildTrayMenu (controller_);
    auto alive = alive_;
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition().withMinimumWidth (280), [this, alive] (int choice)
    {
        if (! alive->load())
            return;
        switch (choice)
        {
            case menuTitle:
            case menuOpen:   showStatusWindow(); break;
            case menuPause:  pauseMonitoring(); break;
            case menuResume: resumeMonitoring(); break;
            case menuScan:   scanNow(); break;
            case menuExit:   confirmExit(); break;
            default: break;
        }
    });
}

void AudioslaveApplication::showStatusWindow()
{
    if (quitting_)
        return;
    if (window_ == nullptr)
    {
        StatusWindow::Actions actions;
        actions.pause = [this] { pauseMonitoring(); };
        actions.resume = [this] { resumeMonitoring(); };
        actions.scan = [this] { scanNow(); };
        actions.openLogs = [this] { openLogs(); };
        auto alive = alive_;
        window_ = std::make_unique<StatusWindow> (std::move (actions), [this, alive]
        {
            // Destroy after the close-button callback has returned.
            juce::MessageManager::callAsync ([this, alive] { if (alive->load()) window_.reset(); });
        });
    }
    window_->update (controller_);
    window_->setVisible (true);
    window_->setMinimised (false);
    window_->toFront (true);
}

void AudioslaveApplication::sendCommand (ipc::Command command, const juce::String& failureText,
                                         std::function<void (bool)> done)
{
    if (client_ == nullptr)
        return;
    const bool action = command != ipc::Command::status;
    if (action)
    {
        controller_.setBusy (true);
        refresh();
        Logger::instance().info ("Tray: " + ipc::commandName (command) + " requested by the user.");
    }

    auto alive = alive_;
    client_->send (command, [this, alive, action, failureText, done = std::move (done), command] (const ipc::Reply& reply)
    {
        if (! alive->load())
            return;
        if (action)
            controller_.setBusy (false);
        if (reply.status)
            controller_.setStatus (*reply.status);

        const bool ok = reply.delivered && reply.ok;
        if (! ok && action)
        {
            Logger::instance().error ("Tray: " + ipc::commandName (command) + " failed: " + reply.error);
            if (failureText.isNotEmpty())
                showError (failureText + "\n" + reply.error);
        }
        refresh();
        if (done)
            done (ok);
    });
}

void AudioslaveApplication::pauseMonitoring()
{
    sendCommand (ipc::Command::pause, utf8 ("Não foi possível pausar o monitoramento:"));
}

void AudioslaveApplication::resumeMonitoring()
{
    switch (controller_.resumeAction())
    {
        case TrayController::ResumeAction::resumeThroughHost:
            sendCommand (ipc::Command::resume, utf8 ("Não foi possível retomar o monitoramento:"));
            break;
        case TrayController::ResumeAction::startService:
            startServiceInBackground (true);
            break;
        case TrayController::ResumeAction::none:
            break;
    }
}

void AudioslaveApplication::scanNow()
{
    sendCommand (ipc::Command::scan, utf8 ("Não foi possível iniciar a verificação:"));
}

void AudioslaveApplication::startServiceInBackground (bool reportErrors)
{
    controller_.setBusy (true);
    refresh();
    Logger::instance().info ("Tray: starting the Audioslave service.");
    runInBackground ([]
                     {
                         juce::String error;
                         scm::start (error);
                         return error;
                     },
                     [this, reportErrors] (const juce::String& error)
                     {
                         controller_.setBusy (false);
                         if (error.isNotEmpty())
                         {
                             Logger::instance().warn ("Tray: could not start the service: " + error);
                             if (reportErrors)
                                 showError (utf8 ("Não foi possível iniciar o serviço Audioslave:\n") + error);
                         }
                         timerCallback();
                     });
}

void AudioslaveApplication::startPortableHost()
{
    portableHost_ = std::make_unique<PortableHost>();
    if (! portableHost_->start())
    {
        Logger::instance().error ("Tray: could not start the portable monitoring thread.");
        showError (utf8 ("Não foi possível iniciar o monitoramento de áudio."));
    }
}

void AudioslaveApplication::confirmExit()
{
    const auto text = serviceMode_
                          ? utf8 ("Encerrar o Audioslave?\n\nO serviço de proteção será parado e o ícone removido. "
                                  "A proteção volta automaticamente na próxima inicialização do Windows ou quando "
                                  "você abrir o Audioslave novamente.")
                          : utf8 ("Encerrar o Audioslave?\n\nO monitoramento dos dispositivos de áudio será interrompido.");
    theme::DialogOptions options;
    options.icon = juce::MessageBoxIconType::QuestionIcon;
    options.title = utf8 ("Encerrar o Audioslave?");
    options.message = text.fromFirstOccurrenceOf ("\n\n", false, false);
    options.buttons = { "Encerrar", "Cancelar" };
    options.destructive = true;
    auto alive = alive_;
    theme::showDialog (std::move (options), [this, alive] (int button, juce::Component*)
    {
        if (alive->load() && button == 0)
            exitAndStop();
    });
}

void AudioslaveApplication::exitAndStop()
{
    if (! serviceMode_)
    {
        quit(); // shutdown() stops the portable host
        return;
    }
    if (client_ != nullptr && client_->isConnected())
    {
        // The service stops itself with exit code 0: no automatic restart.
        sendCommand (ipc::Command::stop, {}, [this] (bool ok)
        {
            if (ok)
            {
                quit();
                return;
            }
            // Fall back to the SCM.
            runInBackground ([]
                             {
                                 juce::String error;
                                 scm::stop (error);
                                 return error;
                             },
                             [this] (const juce::String& error)
                             {
                                 if (error.isNotEmpty())
                                     Logger::instance().error ("Tray: stop failed: " + error);
                                 quit();
                             });
        });
        return;
    }

    controller_.setBusy (true);
    refresh();
    runInBackground ([]
                     {
                         juce::String error;
                         scm::stop (error);
                         return error;
                     },
                     [this] (const juce::String& error)
                     {
                         if (error.isNotEmpty())
                         {
                             Logger::instance().error ("Tray: stop failed: " + error);
                             auto alive = alive_;
                             showAlert (juce::MessageBoxIconType::WarningIcon,
                                        utf8 ("Não foi possível parar o serviço Audioslave:\n") + error
                                            + utf8 ("\n\nO ícone será fechado, mas a proteção continua ativa em segundo plano."),
                                        [this, alive] (int) { if (alive->load()) quit(); });
                             return;
                         }
                         quit();
                     });
}

void AudioslaveApplication::openLogs()
{
    const auto dir = controller_.status() ? juce::File (controller_.status()->logsDir) : paths::logsDir();
    paths::ensureDirectory (dir);
    dir.startAsProcess();
}

void AudioslaveApplication::showError (const juce::String& message)
{
    showAlert (juce::MessageBoxIconType::WarningIcon, message);
}

void AudioslaveApplication::runInBackground (std::function<juce::String()> job,
                                             std::function<void (const juce::String&)> done)
{
    auto alive = alive_;
    pool_.addJob ([alive, job = std::move (job), done = std::move (done)]
    {
        auto error = job();
        juce::MessageManager::callAsync ([alive, done, error]
        {
            if (alive->load() && done)
                done (error);
        });
    });
}
} // namespace audioslave
