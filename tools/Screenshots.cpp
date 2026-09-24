// Development tool (not shipped): renders the Audioslave UI with sample data
// and saves PNG snapshots, so the design can be reviewed without driving the
// real tray with mouse or keyboard.
//
//   audioslave_screenshots <output folder>

#include "platform/windows/WinCommon.h"
#include "app/Dialog.h"
#include "app/StatusWindow.h"
#include "app/Theme.h"
#include "app/TrayController.h"
#include "app/TrayMenu.h"
#include "installer/SetupWizard.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace audioslave
{
namespace
{
ipc::StatusSnapshot sampleStatus (EngineState state)
{
    ipc::StatusSnapshot s;
    s.version = "2.0.0";
    s.mode = "service";
    s.state = state;
    s.exclusiveProtection = true;
    s.formatStandardization = true;
    s.formatTarget = "48000 Hz / 24-bit";
    s.hasScanned = true;
    s.lastScanTime = juce::Time::getCurrentTime();
    s.lastScan.endpointsScanned = 9;
    s.totalExclusiveFixes = 3;
    s.totalFormatChanges = 1;
    s.logsDir = "C:\\Program Files\\Audioslave\\logs";
    struct Row
    {
        const char* name;
        EndpointFlow flow;
        EndpointState state;
        bool isDefault;
        const char* exclusive;
        const char* format;
    };
    const Row rows[] = {
        { "Speakers / Headphones (Realtek Audio)", EndpointFlow::render, EndpointState::active, true, "blocked", "48000 Hz / 24-bit" },
        { "Speakers 01 (ASIOVADPRO Driver)", EndpointFlow::render, EndpointState::active, false, "blocked", "48000 Hz / 24-bit" },
        { "Speakers 02 (ASIOVADPRO Driver)", EndpointFlow::render, EndpointState::active, false, "blocked", "48000 Hz / 24-bit" },
        { "Fones de ouvido (JBL Tune 520BT)", EndpointFlow::render, EndpointState::unplugged, false, "blocked", "" },
        { "Microphone (Realtek Audio)", EndpointFlow::capture, EndpointState::active, true, "blocked", "48000 Hz / 24-bit" },
        { "Webcam 1 (NDI Webcam Audio)", EndpointFlow::capture, EndpointState::active, false, "blocked", "44100 Hz / 16-bit" },
        { "Mix 01 (ASIOVADPRO Driver)", EndpointFlow::capture, EndpointState::active, false, "allowed", "48000 Hz / 24-bit" },
        { "Headset (QCY H2 Pro)", EndpointFlow::capture, EndpointState::unplugged, false, "blocked", "" },
    };
    for (const auto& r : rows)
    {
        EndpointStatus e;
        e.name = juce::String (juce::CharPointer_UTF8 (r.name));
        e.flow = r.flow;
        e.state = r.state;
        e.isDefault = r.isDefault;
        e.exclusive = r.exclusive;
        e.format = r.format;
        s.endpoints.push_back (e);
    }
    return s;
}

TrayController controllerFor (EngineState state)
{
    TrayController c;
    c.setConnected (true);
    c.setStatus (sampleStatus (state));
    return c;
}

// Paints the real tray menu (same items, same LookAndFeel calls as JUCE's
// menu window) into an image. Needed because JUCE dismisses menus of a
// process that does not own the foreground window.
juce::Image renderMenu (const juce::PopupMenu& menu, juce::LookAndFeel& lf, float scale)
{
    const int border = lf.getPopupMenuBorderSize();
    struct Entry
    {
        const juce::PopupMenu::Item* item;
        int height;
    };
    std::vector<Entry> entries;
    int width = 280, height = 2 * border;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        const auto& item = it.getItem();
        int w = 0, h = 0;
        if (item.customComponent != nullptr)
            item.customComponent->getIdealSize (w, h);
        else
            lf.getIdealPopupMenuItemSize (item.text, item.isSeparator, 24, w, h);
        width = juce::jmax (width, w + 2 * border);
        entries.push_back ({ &item, h });
        height += h;
    }

    juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) width * scale), juce::roundToInt ((float) height * scale), true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (scale));
    lf.drawPopupMenuBackgroundWithOptions (g, width, height, juce::PopupMenu::Options());
    int y = border;
    for (const auto& e : entries)
    {
        const auto area = juce::Rectangle<int> (border, y, width - 2 * border, e.height);
        const auto& item = *e.item;
        if (item.customComponent != nullptr)
        {
            item.customComponent->setBounds (area);
            juce::Graphics::ScopedSaveState state (g);
            g.setOrigin (area.getPosition());
            item.customComponent->paintEntireComponent (g, true);
        }
        else
        {
            lf.drawPopupMenuItem (g, area, item.isSeparator, item.isEnabled, false, item.isTicked, item.subMenu != nullptr,
                                  item.text, item.shortcutKeyDescription, item.image.get(),
                                  item.colour != juce::Colour() ? &item.colour : nullptr);
        }
        y += e.height;
    }
    return image;
}

bool save (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    return out.openedOk() && png.writeImageToStream (image, out);
}
} // namespace

class ScreenshotApplication final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "audioslave_screenshots"; }
    const juce::String getApplicationVersion() override { return "1"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel_);
        out_ = juce::File::getCurrentWorkingDirectory().getChildFile (commandLine.unquoted().trim());
        out_.createDirectory();

        // Status window (running) and a JUCE dialog.
        {
            StatusWindow window ({}, {});
            window.update (controllerFor (EngineState::running));
            save (window.createComponentSnapshot (window.getLocalBounds(), true, 1.5f), out_.getChildFile ("status-window.png"));
        }
        {
            setup::Options options;
            options.dir = "C:\\Program Files\\Audioslave";
            setup::SetupWizard wizard (options, [] (int) {});
            save (wizard.createComponentSnapshot (wizard.getLocalBounds(), true, 1.5f), out_.getChildFile ("installer.png"));
        }

        // The tray menu in both states, plus one hovered item.
        save (renderMenu (buildTrayMenu (controllerFor (EngineState::running)), lookAndFeel_, 1.5f),
              out_.getChildFile ("tray-menu.png"));
        save (renderMenu (buildTrayMenu (controllerFor (EngineState::paused)), lookAndFeel_, 1.5f),
              out_.getChildFile ("tray-menu-paused.png"));

        // Dialogs are real windows: shown briefly and captured.
        step_ = 2;
        showStep (step_);
        startTimer (700);
    }

    void shutdown() override { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }

private:
    // Each step shows something; the next timer tick captures it.
    void showMenu (EngineState state)
    {
        controller_ = controllerFor (state);
        juce::Process::makeForegroundProcess(); // JUCE dismisses menus of background processes
        buildTrayMenu (controller_).showMenuAsync (
            juce::PopupMenu::Options().withTargetScreenArea ({ 400, 300, 1, 1 }).withMinimumWidth (280), [] (int) {});
    }

    void showStep (int step)
    {
        switch (step)
        {
            case 0: showMenu (EngineState::running); break;
            case 1: showMenu (EngineState::paused); break;
            case 2:
            {
                theme::DialogOptions o;
                o.icon = juce::MessageBoxIconType::QuestionIcon;
                o.title = juce::String (juce::CharPointer_UTF8 ("Encerrar o Audioslave?"));
                o.message = juce::String (juce::CharPointer_UTF8 (
                    "O serviço de proteção será parado e o ícone removido. A proteção volta automaticamente na próxima "
                    "inicialização do Windows ou quando você abrir o Audioslave novamente."));
                o.buttons = { "Encerrar", "Cancelar" };
                o.destructive = true;
                theme::showDialog (std::move (o));
                break;
            }
            case 3:
            {
                theme::DialogOptions o;
                o.icon = juce::MessageBoxIconType::WarningIcon;
                o.title = juce::String (juce::CharPointer_UTF8 ("Desinstalar o Audioslave?"));
                o.message = juce::String (juce::CharPointer_UTF8 (
                    "O serviço, a aplicação da bandeja, os atalhos e os arquivos do programa serão removidos. As "
                    "configurações atuais dos dispositivos de áudio não são revertidas."));
                o.buttons = { "Desinstalar", "Cancelar" };
                o.destructive = true;
                auto toggle = std::make_unique<juce::ToggleButton> (juce::String (juce::CharPointer_UTF8 ("Remover também os logs e a configuração")));
                toggle->setSize (380, 28);
                o.extra = std::move (toggle);
                theme::showDialog (std::move (o));
                break;
            }
            default: break;
        }
    }

    void timerCallback() override
    {
        static const char* names[] = { "tray-menu.png", "tray-menu-paused.png", "dialog-exit.png", "dialog-uninstall.png" };
        auto& desktop = juce::Desktop::getInstance();
        bool captured = false;
        for (int i = 0; i < desktop.getNumComponents(); ++i)
        {
            auto* c = desktop.getComponent (i);
            std::printf ("step %d: desktop component %d visible=%d %dx%d\n", step_, i, c != nullptr && c->isVisible() ? 1 : 0,
                         c != nullptr ? c->getWidth() : 0, c != nullptr ? c->getHeight() : 0);
            if (c != nullptr && c->isVisible() && c->getWidth() > 100 && c->getHeight() > 100)
            {
                save (c->createComponentSnapshot (c->getLocalBounds(), true, 1.5f), out_.getChildFile (names[step_]));
                captured = true;
            }
        }
        if (! captured)
            std::printf ("step %d: nothing captured\n", step_);
        juce::PopupMenu::dismissAllActiveMenus();
        if (auto* modal = juce::Component::getCurrentlyModalComponent())
            modal->exitModalState (-1);

        if (++step_ < 4)
        {
            showStep (step_);
            return;
        }
        stopTimer();
        quit();
    }

    int step_ = 0;
    theme::LookAndFeel lookAndFeel_;
    juce::File out_;
    TrayController controller_;
};
} // namespace audioslave

START_JUCE_APPLICATION (audioslave::ScreenshotApplication)
