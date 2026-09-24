#include "platform/windows/WinCommon.h"
#include "app/AudioslaveApplication.h"
#include "AudioslaveVersion.h"
#include "app/Dialog.h"
#include "app/SettingsReport.h"
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
    tooltips_ = std::make_unique<juce::TooltipWindow> (nullptr, 600);

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
    tray_->onMenu = [this] (juce::Rectangle<int> iconArea) { showMenu (iconArea); };

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
    juce::PopupMenu::dismissAllActiveMenus();
    window_.reset();
    tooltips_.reset();
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
    statusReceived (status);
}

void AudioslaveApplication::statusReceived (const ipc::StatusSnapshot& status)
{
    controller_.setStatus (status);
    refresh();
    notifyDeviceEvents (status);
    promptPendingDisable (status);
}

void AudioslaveApplication::notifyDeviceEvents (const ipc::StatusSnapshot& status)
{
    juce::int64 newest = lastEventSequence_;
    juce::StringArray disabled, reenabled, other;
    for (const auto& e : status.events)
    {
        newest = juce::jmax (newest, e.sequence);
        // Changes the user just applied are reported by the settings screen.
        if (lastEventSequence_ < 0 || e.sequence <= lastEventSequence_ || e.interactive)
            continue;
        switch (e.action)
        {
            case DeviceAction::disabled:    disabled.add (e.name); break;
            case DeviceAction::reenabled:   reenabled.add (e.name); break;
            case DeviceAction::leftEnabled:
                other.add (e.name + utf8 (" voltou a ser habilitado várias vezes e não será mais desabilitado até a configuração mudar."));
                break;
            case DeviceAction::failed:      other.add (utf8 ("Falha em ") + e.name + ": " + e.reason); break;
            default: break;
        }
    }
    lastEventSequence_ = juce::jmax<juce::int64> (newest, 0);
    if (tray_ == nullptr || (disabled.isEmpty() && reenabled.isEmpty() && other.isEmpty()))
        return;

    juce::StringArray lines;
    if (! disabled.isEmpty())
        lines.add ((disabled.size() == 1 ? utf8 ("Desabilitado (não suporta a configuração selecionada): ")
                                         : utf8 ("Desabilitados (não suportam a configuração selecionada): "))
                   + disabled.joinIntoString (", "));
    if (! reenabled.isEmpty())
        lines.add (utf8 ("Reativado: ") + reenabled.joinIntoString (", "));
    lines.addArray (other);
    Logger::instance().info ("Tray: device notification: " + lines.joinIntoString (" | "));
    tray_->showInfoBubble (utf8 ("Audioslave — dispositivos de áudio"), lines.joinIntoString ("\n"));
}

void AudioslaveApplication::promptPendingDisable (const ipc::StatusSnapshot& status)
{
    // The policy was turned on outside the window (config.ini) and has not
    // been confirmed: ask once per session, never disable silently.
    if (pendingPromptShown_ || status.pendingDisable.empty() || ! status.disableIncompatibleDevices
        || status.disablePolicyConfirmed || quitting_)
        return;
    pendingPromptShown_ = true;

    ipc::AudioSettings settings;
    settings.formatStandardization = status.formatStandardization;
    settings.sampleRate = static_cast<std::uint32_t> (status.sampleRate);
    settings.bitDepth = static_cast<std::uint16_t> (status.bitDepth);
    settings.disableIncompatibleDevices = true;
    auto preview = buildPreview (settings, status.pendingDisable);

    theme::DialogOptions options;
    options.icon = juce::MessageBoxIconType::WarningIcon;
    options.title = utf8 ("Confirmar a desativação de dispositivos");
    options.message = utf8 ("Alguns dispositivos de áudio não suportam ") + status.formatTarget
                      + utf8 (". A configuração atual desabilita os dispositivos incompatíveis; os seguintes dispositivos "
                              "poderão ser desabilitados. Cancelar desliga essa opção sem desabilitar nada.");
    options.buttons = { "Continuar", "Cancelar" };
    options.destructive = true;
    auto details = std::make_unique<juce::TextEditor>();
    details->setMultiLine (true, true);
    details->setReadOnly (true);
    details->setCaretVisible (false);
    details->setFont (theme::font (13.5f));
    details->setColour (juce::TextEditor::backgroundColourId, theme::surface);
    details->setColour (juce::TextEditor::textColourId, theme::textDim);
    details->setIndents (10, 8);
    details->setText (preview.details, false);
    details->setSize (100, juce::jlimit (60, 260, juce::StringArray::fromLines (preview.details).size() * 19 + 20));
    options.extra = std::move (details);

    auto alive = alive_;
    theme::showDialog (std::move (options), [this, alive, settings] (int button, juce::Component*)
    {
        if (! alive->load())
            return;
        auto chosen = settings;
        chosen.confirmDisable = button == 0;
        chosen.disableIncompatibleDevices = button == 0;
        Logger::instance().info (juce::String ("Tray: the user ") + (button == 0 ? "confirmed" : "declined")
                                 + " the incompatible-device policy.");
        request (ipc::Command::configure, ipc::toVar (chosen), [chosen] (const ipc::Reply& reply)
        {
            if (! reply.delivered || ! reply.ok)
            {
                theme::showMessage (juce::MessageBoxIconType::WarningIcon, utf8 ("Não foi possível aplicar a configuração"),
                                    reply.error);
                return;
            }
            const auto outcome = buildOutcome (chosen, ipc::deviceReportsFromVar (reply.result.getProperty ("devices", {})),
                                               reply.result.getProperty ("paused", false));
            theme::showMessage (outcome.problems ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::InfoIcon,
                                chosen.confirmDisable ? outcome.title : utf8 ("Opção desligada"),
                                chosen.confirmDisable ? outcome.message + "\n\n" + outcome.details
                                                      : utf8 ("Nenhum dispositivo foi desabilitado."));
        });
    });
}

void AudioslaveApplication::request (ipc::Command command, const juce::var& args, std::function<void (const ipc::Reply&)> done)
{
    if (client_ == nullptr || ! client_->isConnected())
    {
        ipc::Reply reply;
        reply.error = utf8 ("Sem conexão com o serviço Audioslave.");
        if (done)
            done (reply);
        return;
    }
    Logger::instance().info ("Tray: " + ipc::commandName (command) + " requested by the user.");
    auto alive = alive_;
    client_->send (command, args, [this, alive, done = std::move (done)] (const ipc::Reply& reply)
    {
        if (! alive->load())
            return;
        if (reply.status)
            statusReceived (*reply.status);
        if (done)
            done (reply);
    });
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
void AudioslaveApplication::showMenu (juce::Rectangle<int> iconArea)
{
    if (quitting_)
        return;
    auto alive = alive_;
    TrayMenuActions actions;
    // Run in place: the menu stays open and follows the live status.
    actions.pause = [this, alive] { if (alive->load()) pauseMonitoring(); };
    actions.resume = [this, alive] { if (alive->load()) resumeMonitoring(); };
    actions.scan = [this, alive] { if (alive->load()) scanNow(); };
    actions.live = [this, alive]() -> const TrayController* { return alive->load() && ! quitting_ ? &controller_ : nullptr; };

    auto options = juce::PopupMenu::Options().withMinimumWidth (280);
    // Next to the icon (never over it), also inside the hidden-icons flyout.
    options = iconArea.isEmpty() ? options.withMousePosition() : options.withTargetScreenArea (iconArea);
    buildTrayMenu (controller_, actions).showMenuAsync (options, [this, alive] (int choice)
    {
        if (! alive->load())
            return;
        switch (choice)
        {
            case menuTitle:
            case menuOpen:     showStatusWindow(); break;
            case menuSettings: showSettings(); break;
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
        actions.request = [this] (ipc::Command command, const juce::var& args, std::function<void (const ipc::Reply&)> done)
        {
            request (command, args, std::move (done));
        };
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

void AudioslaveApplication::showSettings()
{
    showStatusWindow();
    if (window_ != nullptr)
        window_->showSettings (true);
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
            statusReceived (*reply.status);

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
