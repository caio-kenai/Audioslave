#include "platform/windows/WinCommon.h"
#include "cli/Commands.h"
#include "AudioslaveVersion.h"
#include "audio/windows/WindowsAudioEndpointEnumerator.h"
#include "audio/windows/WindowsAudioFormatPolicy.h"
#include "audio/windows/WindowsExclusiveModePolicy.h"
#include "cli/Diagnostics.h"
#include "common/Branding.h"
#include "config/Configuration.h"
#include "core/WatchdogEngine.h"
#include "ipc/ControlClient.h"
#include "logging/Logger.h"
#include "platform/windows/Elevation.h"
#include "platform/windows/Paths.h"
#include "platform/windows/ScopedComInit.h"
#include "platform/windows/WinError.h"
#include "service/ServiceController.h"
#include "service/ServiceHost.h"
#include "service/WindowsService.h"

#include <atomic>
#include <cstdio>

namespace audioslave::cli
{
void print (const juce::String& text)
{
    std::fputs (text.toRawUTF8(), stdout);
    std::fflush (stdout);
}

void printLine (const juce::String& text)
{
    print (text + "\n");
}

namespace
{
using juce::ConsoleApplication;

std::atomic<ServiceHost*> consoleHost { nullptr };

BOOL WINAPI consoleCtrlHandler (DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT || type == CTRL_SHUTDOWN_EVENT)
    {
        if (auto* host = consoleHost.load())
            host->requestStop();
        return TRUE;
    }
    return FALSE;
}

juce::StringArray rawArguments (const juce::ArgumentList& args)
{
    juce::StringArray out;
    for (const auto& a : args.arguments)
        out.add (a.text);
    return out;
}

// Relaunches the same command elevated (it opens its own console window,
// which waits for Enter so its output can be read).
[[noreturn]] void relaunchElevated (const juce::String& what, const juce::ArgumentList& args)
{
    auto arguments = rawArguments (args);
    arguments.addIfNotAlreadyThere ("--wait");
    if (win::launchElevated (paths::executableFile(), arguments))
        ConsoleApplication::fail (what + " requires administrator rights: continuing in the elevated window.", 1);
    ConsoleApplication::fail (what + " requires administrator rights; run it from an elevated prompt.", 1);
}

void requireAdmin (const juce::String& what, const juce::ArgumentList& args)
{
    if (! win::isElevated())
        relaunchElevated (what, args);
}

// Runs an SCM operation; without elevation it is tried first (the service
// DACL lets interactive users start / stop / pause it) and elevated only when
// access is denied.
void scmOperation (const juce::String& what, const juce::ArgumentList& args,
                   const std::function<bool (juce::String&)>& operation, const juce::String& done)
{
    juce::String error;
    if (operation (error))
    {
        printLine (done);
        return;
    }
    if (! win::isElevated() && error.containsIgnoreCase (win::win32ErrorText (ERROR_ACCESS_DENIED)))
        relaunchElevated (what, args);
    ConsoleApplication::fail (what + " failed: " + error, 1);
}

juce::String yesNo (bool b) { return b ? "yes" : "no"; }

void printSnapshot (const ipc::StatusSnapshot& s)
{
    printLine ("Monitoring       : " + engineStateName (s.state) + " (" + s.mode + ", pid " + juce::String (s.pid) + ", v"
               + s.version + ")");
    printLine ("Exclusive mode   : protection " + juce::String (s.exclusiveProtection ? "ENABLED" : "DISABLED"));
    printLine ("Format           : " + (s.formatStandardization ? "standardization ENABLED (" + s.formatTarget + ")"
                                                                 : juce::String ("standardization DISABLED")));
    printLine ("Enforce          : " + juce::String (s.enforce ? "on" : "report only") + " | check every "
               + juce::String (s.checkIntervalSeconds) + " s");
    if (s.hasScanned)
    {
        const auto& r = s.lastScan;
        printLine ("Last scan        : " + s.lastScanTime.formatted ("%Y-%m-%d %H:%M:%S") + " ("
                   + juce::String (r.kind == ScanKind::full ? "full" : "triggered") + ", " + juce::String (r.endpointsScanned)
                   + " endpoint(s), " + juce::String (r.exclusiveFixed) + " fixed, " + juce::String (r.formatApplied)
                   + " format change(s), " + juce::String (r.errors()) + " error(s))");
    }
    printLine ("Since start      : " + juce::String (s.totalScans) + " scan(s), " + juce::String (s.totalExclusiveFixes)
               + " exclusive-mode fix(es), " + juce::String (s.totalFormatChanges) + " format change(s)");
    printLine ("Endpoints        : " + juce::String (static_cast<int> (s.endpoints.size())) + " monitored");
    for (const auto& e : s.endpoints)
        printLine ("  - [" + endpointFlowName (e.flow) + (e.isDefault ? ", default" : "") + "] " + e.name + " | "
                   + endpointStateName (e.state) + " | exclusive " + e.exclusive
                   + (e.format.isNotEmpty() ? " | " + e.format : juce::String()));
    printLine ("Logs             : " + s.logsDir);
}

// Sends a command to the running host (service or portable).
ipc::Reply sendToHost (ipc::Command command)
{
    ipc::ControlClient client (false);
    if (! client.connect())
    {
        ipc::Reply reply;
        reply.error = "the Audioslave service is not reachable";
        return reply;
    }
    auto reply = client.request (command);
    client.disconnect();
    return reply;
}

int commandInstall (const juce::ArgumentList& args)
{
    requireAdmin ("install", args);
    const auto service = paths::siblingExecutable (brand::serviceExecutable);
    juce::String error;
    juce::StringArray warnings;
    if (! scm::install (win::quoteArgument (service.getFullPathName()) + " --service", error, warnings))
        ConsoleApplication::fail ("Install failed: " + error, 1);
    for (const auto& w : warnings)
        printLine ("warning: " + w);

    paths::ensureDirectory (paths::programDataDir());
    if (! win::applyConfigDirAcl (paths::programDataDir(), &error))
        printLine ("warning: config folder permissions: " + error);
    for (const auto& w : loadConfiguration (paths::configFile(), true).warnings)
        printLine ("warning: " + w);
    paths::ensureDirectory (paths::logsDir());
    if (! win::applyLogsDirAcl (paths::logsDir(), &error))
        printLine ("warning: logs folder permissions: " + error);

    printLine ("Installed (or updated). Use: AudioslaveService start");
    return 0;
}

int commandUninstall (const juce::ArgumentList& args)
{
    requireAdmin ("uninstall", args);
    juce::String error;
    if (! scm::uninstall (error))
        ConsoleApplication::fail ("Uninstall failed: " + error, 1);
    printLine ("Uninstalled.");
    return 0;
}

int commandStatus()
{
    const auto status = scm::query();
    if (status.state == scm::State::notInstalled)
    {
        printLine ("Service          : NOT installed");
    }
    else
    {
        printLine ("Service          : " + juce::String (brand::serviceDisplayName) + " (" + juce::String (brand::serviceName) + ")");
        printLine ("State            : " + scm::stateName (status.state));
        printLine ("Process ID       : " + (status.processId != 0 ? juce::String (status.processId) : juce::String ("(not running)")));
        printLine ("Start type       : " + status.startType);
        if (status.processId != 0)
            printLine ("Accepts          : " + juce::String (status.acceptsStop ? "STOP " : "")
                       + (status.acceptsPreshutdown ? "PRESHUTDOWN " : "") + (status.acceptsPauseContinue ? "PAUSE_CONTINUE" : ""));
        if (status.error.isNotEmpty())
            printLine ("Query error      : " + status.error);
    }

    const auto reply = sendToHost (ipc::Command::status);
    if (reply.delivered && reply.status)
    {
        printLine();
        printSnapshot (*reply.status);
        return 0;
    }
    if (status.state == scm::State::running || status.state == scm::State::paused)
        printLine ("Control channel  : unavailable (" + reply.error + ")");
    return status.state == scm::State::notInstalled ? 1 : 0;
}

int commandPauseResume (bool pause, const juce::ArgumentList& args)
{
    const auto reply = sendToHost (pause ? ipc::Command::pause : ipc::Command::resume);
    if (reply.delivered)
    {
        if (! reply.ok)
            ConsoleApplication::fail ((pause ? "Pause failed: " : "Resume failed: ") + reply.error, 1);
        printLine (pause ? "Monitoring paused." : "Monitoring resumed (full scan scheduled).");
        return 0;
    }
    // No control channel: fall back to the SCM.
    scmOperation (pause ? "pause" : "resume", args, [pause] (juce::String& e) { return pause ? scm::pause (e) : scm::resume (e); },
                  pause ? "Monitoring paused." : "Monitoring resumed (full scan scheduled).");
    return 0;
}

int commandRescan()
{
    const auto reply = sendToHost (ipc::Command::scan);
    if (! reply.delivered)
        ConsoleApplication::fail ("Rescan failed: " + reply.error, 1);
    if (! reply.ok)
        ConsoleApplication::fail ("Rescan not started: " + reply.error, 1);
    printLine ("Full scan requested from the running service.");
    return 0;
}

int commandScan()
{
    const win::ScopedComInit com;
    if (! com.isUsable())
        ConsoleApplication::fail ("COM initialisation failed: " + win::hresultText (com.result()), 2);

    const auto loaded = loadConfiguration (paths::configFile(), false);
    Logger::instance().configure ({}, loaded.config.logLevel, true);
    for (const auto& w : loaded.warnings)
        Logger::instance().warn (w);

    win::WindowsAudioEndpointEnumerator enumerator;
    win::WindowsExclusiveModePolicy exclusive;
    win::WindowsAudioFormatPolicy format;
    WatchdogEngine engine (enumerator, exclusive, format, loaded.config);
    const auto r = engine.scanOnce (ScanKind::full);

    printLine ("Scan complete: " + juce::String (r.endpointsScanned) + " endpoint(s)");
    printLine ("  exclusive mode : already off " + juce::String (r.exclusiveAlreadyOff) + " | fixed " + juce::String (r.exclusiveFixed)
               + " | failed " + juce::String (r.exclusiveFailed) + " | skipped " + juce::String (r.exclusiveSkipped));
    if (loaded.config.formatStandardization)
        printLine ("  format " + describeFormatTarget (loaded.config) + " : ok " + juce::String (r.formatCompliant) + " | applied "
                   + juce::String (r.formatApplied) + " | unsupported " + juce::String (r.formatUnsupported) + " | failed "
                   + juce::String (r.formatFailed));
    else
        printLine ("  format standardization disabled");
    if (r.exclusiveFixed + r.formatApplied > 0 && ! win::isElevated())
        printLine ("note: without administrator rights changes may be rejected.");
    return r.errors() > 0 ? 1 : 0;
}

int runHost (ServiceHost::Mode mode)
{
    ServiceHost::Options options;
    options.mode = mode;
    ServiceHost host (options);
    consoleHost = &host;
    if (mode == ServiceHost::Mode::console)
    {
        ::SetConsoleCtrlHandler (consoleCtrlHandler, TRUE);
        printLine ("Running in the foreground. Press Ctrl+C to stop.");
    }
    const int rc = host.run();
    consoleHost = nullptr;
    if (mode == ServiceHost::Mode::console)
        ::SetConsoleCtrlHandler (consoleCtrlHandler, FALSE);
    Logger::instance().close();
    return rc;
}

juce::String usage()
{
    return juce::String ("Audioslave ") + AUDIOSLAVE_VERSION_STRING
           + " - keeps Windows audio devices out of exclusive mode\n\n"
             "Usage: AudioslaveService <command> [options]\n";
}

void addCommand (ConsoleApplication& app, const char* option, const char* argsText, const char* description,
                 std::function<int (const juce::ArgumentList&)> body)
{
    // JUCE's help lists "<exe> <argumentDescription>", so it carries the verb.
    const auto shown = (juce::String (option) + " " + argsText).trim();
    app.addCommand ({ option, shown, description, {}, [body = std::move (body)] (const juce::ArgumentList& a)
                      {
                          if (const int rc = body (a); rc != 0)
                              ConsoleApplication::fail ({}, rc);
                      } });
}
} // namespace

int run (const juce::ArgumentList& args)
{
    ConsoleApplication app;
    app.addHelpCommand ("help|--help|-h|/?", usage(), false);
    app.addVersionCommand ("version|--version", juce::String ("Audioslave version ") + AUDIOSLAVE_VERSION_STRING + " (JUCE "
                                                     + juce::SystemStats::getJUCEVersion().fromFirstOccurrenceOf ("v", false, false) + ")");

    addCommand (app, "install", "", "Install (or update) the Windows service", commandInstall);
    addCommand (app, "uninstall", "", "Remove the Windows service", commandUninstall);
    addCommand (app, "start", "", "Start the service", [] (const juce::ArgumentList& a)
                {
                    scmOperation ("start", a, [] (juce::String& e) { return scm::start (e); }, "Started.");
                    return 0;
                });
    addCommand (app, "stop", "", "Stop the service (or the portable / console host)", [] (const juce::ArgumentList& a)
                {
                    if (! scm::exists())
                    {
                        // No service: stop whichever host answers on the control pipe.
                        const auto reply = sendToHost (ipc::Command::stop);
                        if (! reply.delivered)
                            ConsoleApplication::fail ("Stop failed: the service is not installed and no host is running.", 1);
                        printLine ("Stopped.");
                        return 0;
                    }
                    scmOperation ("stop", a, [] (juce::String& e) { return scm::stop (e); }, "Stopped.");
                    return 0;
                });
    addCommand (app, "restart", "", "Restart the service", [] (const juce::ArgumentList& a)
                {
                    scmOperation ("restart", a, [] (juce::String& e) { return scm::restart (e); }, "Restarted.");
                    return 0;
                });
    addCommand (app, "status", "", "Service state and live monitoring status", [] (const juce::ArgumentList&) { return commandStatus(); });
    addCommand (app, "pause", "", "Pause monitoring (nothing is modified while paused)",
                [] (const juce::ArgumentList& a) { return commandPauseResume (true, a); });
    addCommand (app, "resume", "", "Resume monitoring and run a full scan",
                [] (const juce::ArgumentList& a) { return commandPauseResume (false, a); });
    addCommand (app, "rescan", "", "Ask the running service for a full scan now", [] (const juce::ArgumentList&) { return commandRescan(); });
    addCommand (app, "scan", "", "Run one enforcement pass in this process", [] (const juce::ArgumentList&) { return commandScan(); });
    addCommand (app, "devices", "", "List endpoints: exclusive mode, formats, JUCE device names",
                [] (const juce::ArgumentList&) { return runDevices(); });
    addCommand (app, "diagnose", "[--probe-exclusive]", "Full self-check (the probe opens devices through JUCE)",
                [] (const juce::ArgumentList& a) { return runDiagnose (a.containsOption ("--probe-exclusive|--probe")); });
    addCommand (app, "validate", "", "Check with JUCE that no endpoint can be opened exclusively",
                [] (const juce::ArgumentList&) { return runValidate(); });
    addCommand (app, "run|--foreground", "", "Run the watchdog in this console (Ctrl+C to stop)",
                [] (const juce::ArgumentList&) { return runHost (ServiceHost::Mode::console); });
    addCommand (app, "--portable", "", "Internal: monitoring host started by the tray when no service is installed",
                [] (const juce::ArgumentList&)
                {
                    if (scm::exists())
                        ConsoleApplication::fail ("The Audioslave service is installed; the portable host is not needed.", 1);
                    return runHost (ServiceHost::Mode::portable);
                });
    addCommand (app, "--service", "", "Internal: entry point used by the Service Control Manager",
                [] (const juce::ArgumentList&)
                {
                    if (! runWindowsService())
                    {
                        if (::GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
                            ConsoleApplication::fail ("Not started by the Service Control Manager: install the service, "
                                                      "or use 'AudioslaveService run' in a console.", 1);
                        ConsoleApplication::fail ("StartServiceCtrlDispatcher failed: " + win::lastErrorText(), 1);
                    }
                    return 0;
                });

    // No arguments: help. Unknown verb: help and exit code 1.
    app.addDefaultCommand ({ {}, "(no arguments)", "Show this help", {}, [&app] (const juce::ArgumentList& a)
                             {
                                 printLine (usage());
                                 app.printCommandList (a);
                                 if (! a.arguments.isEmpty())
                                     ConsoleApplication::fail ("Unknown command: " + a[0].text, 1);
                             } });

    // "--wait" is added when a command relaunches itself elevated.
    auto effective = args;
    const bool wait = effective.removeOptionIfFound ("--wait");
    const int rc = app.findAndRunCommand (effective, true);
    if (wait)
    {
        printLine ("\nPress Enter to close this window.");
        std::getchar();
    }
    return rc;
}
} // namespace audioslave::cli
