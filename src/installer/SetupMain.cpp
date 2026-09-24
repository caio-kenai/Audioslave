// Audioslave-Setup.exe / Uninstall.exe (JUCE GUI application, elevated).

#include "platform/windows/WinCommon.h"
#include "AudioslaveAssets.h"
#include "AudioslaveVersion.h"
#include "app/Dialog.h"
#include "app/Theme.h"
#include "common/Branding.h"
#include "common/Strings.h"
#include "installer/InstallerCore.h"
#include "installer/SetupWizard.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace audioslave::setup
{
class SetupWindow final : public juce::DocumentWindow
{
public:
    SetupWindow (Options options, std::function<void (int)> onFinished)
        : juce::DocumentWindow (utf8 ("Instalação do Audioslave"), theme::background, juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (40);
        setDropShadowEnabled (true);
        wizard_ = new SetupWizard (std::move (options), std::move (onFinished));
        setContentOwned (wizard_, true);
        setIcon (juce::ImageCache::getFromMemory (AudioslaveAssets::normal64_png, AudioslaveAssets::normal64_pngSize));
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { wizard_->requestClose(); }

private:
    SetupWizard* wizard_ = nullptr; // owned by the window
};

class SetupApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Audioslave Setup"; }
    const juce::String getApplicationVersion() override { return AUDIOSLAVE_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel_);
        const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        options_ = parseOptions (getCommandLineParameterArray(), self.getFileName());

        if (options_.uninstall)
        {
            runUninstaller();
            return;
        }

        if (options_.dir.isEmpty())
            options_.dir = defaultInstallDir().getFullPathName();

        if (options_.silent)
        {
            juce::String error;
            const bool ok = install (options_, [] (int, const juce::String&) {}, error);
            finish (ok ? 0 : 1);
            return;
        }

        window_ = std::make_unique<SetupWindow> (options_, [this] (int code)
        {
            juce::MessageManager::callAsync ([this, code] { finish (code); });
        });
    }

    void shutdown() override
    {
        window_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (window_ != nullptr)
            window_->closeButtonPressed();
        else
            quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    void finish (int code)
    {
        setApplicationReturnValue (code);
        quit();
    }

    juce::File uninstallDir() const
    {
        if (options_.dir.isNotEmpty())
            return juce::File (options_.dir);
        const auto location = installedLocation();
        if (location.isNotEmpty())
            return juce::File (location);
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    }

    void runUninstaller()
    {
        if (options_.uninstallStage2 || options_.silent)
        {
            uninstallNow();
            return;
        }

        // Confirmation with the "also remove logs and configuration" option.
        theme::DialogOptions dialog;
        dialog.icon = juce::MessageBoxIconType::WarningIcon;
        dialog.title = utf8 ("Desinstalar o Audioslave?");
        dialog.message = utf8 ("O serviço, a aplicação da bandeja, os atalhos e os arquivos do programa serão removidos. "
                               "As configurações atuais dos dispositivos de áudio não são revertidas.");
        dialog.buttons = { "Desinstalar", "Cancelar" };
        dialog.destructive = true;
        auto toggle = std::make_unique<juce::ToggleButton> (utf8 ("Remover também os logs e a configuração"));
        toggle->setSize (380, 28);
        dialog.extra = std::move (toggle);
        theme::showDialog (std::move (dialog), [this] (int result, juce::Component* extra)
        {
            if (result != 0)
            {
                finish (1);
                return;
            }
            if (auto* t = dynamic_cast<juce::ToggleButton*> (extra))
                options_.removeData = t->getToggleState();
            // Continue from %TEMP% so this folder can be removed.
            if (relaunchUninstallerFromTemp (options_, uninstallDir()))
            {
                finish (0);
                return;
            }
            uninstallNow(); // could not relaunch: the uninstaller is deleted at reboot
        });
    }

    void uninstallNow()
    {
        const auto dir = uninstallDir();
        juce::String error;
        const bool ok = uninstall (dir, options_.removeData, error);
        if (options_.uninstallStage2)
            deleteSelfLater (dir);

        if (options_.silent)
        {
            finish (ok ? 0 : 1);
            return;
        }
        const auto message = ok ? (options_.removeData
                                       ? utf8 ("O Audioslave foi removido.")
                                       : utf8 ("O Audioslave foi removido.\n\nOs logs foram mantidos na pasta de instalação e a "
                                               "configuração em ProgramData."))
                                : error;
        theme::showMessage (ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                            ok ? utf8 ("Audioslave removido") : utf8 ("A desinstalação falhou"), message,
                            [this, ok] { finish (ok ? 0 : 1); });
    }

    theme::LookAndFeel lookAndFeel_;
    Options options_;
    std::unique_ptr<SetupWindow> window_;
};
} // namespace audioslave::setup

START_JUCE_APPLICATION (audioslave::setup::SetupApplication)
