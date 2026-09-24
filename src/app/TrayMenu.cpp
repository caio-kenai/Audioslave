#include "app/TrayMenu.h"
#include "app/Theme.h"
#include "app/TrayIcon.h"
#include "common/Branding.h"

namespace audioslave
{
namespace
{
// Menu header: logo, product name and the status pill. Clicking it opens
// the status window.
class MenuHeader final : public juce::PopupMenu::CustomComponent
{
public:
    MenuHeader (juce::String status, juce::Colour colour)
        : juce::PopupMenu::CustomComponent (true), status_ (std::move (status)), colour_ (colour)
    {
        logo_ = TrayIcon::logoImage (false, 128);
    }

    void getIdealSize (int& width, int& height) override
    {
        width = 280;
        height = 66;
    }

    void paint (juce::Graphics& g) override
    {
        if (isItemHighlighted())
        {
            g.setColour (theme::surfaceRaised);
            g.fillRoundedRectangle (getLocalBounds().reduced (5, 2).toFloat(), 6.0f);
        }
        auto r = getLocalBounds().reduced (14, 10).toFloat();
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logo_, r.removeFromLeft (42.0f).withSizeKeepingCentre (42.0f, 42.0f), juce::RectanglePlacement::centred);
        r.removeFromLeft (12.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (16.0f, true));
        g.drawText (brand::productName, r.removeFromTop (r.getHeight() * 0.5f), juce::Justification::bottomLeft, false);
        const auto pillFont = theme::font (11.0f, true);
        const float pillW = juce::jmin (r.getWidth(), juce::GlyphArrangement::getStringWidth (pillFont, status_) + 40.0f);
        theme::paintPill (g, r.withTrimmedTop (5.0f).withHeight (22.0f).withWidth (pillW), colour_, status_);
    }

private:
    juce::String status_;
    juce::Colour colour_;
    juce::Image logo_;
};
} // namespace

juce::Colour trayStateColour (TrayController::UiState s)
{
    switch (s)
    {
        case TrayController::UiState::running:  return theme::ok;
        case TrayController::UiState::paused:   return theme::warning;
        case TrayController::UiState::starting:
        case TrayController::UiState::stopping: return theme::accentLight;
        default:                                return theme::danger;
    }
}

juce::PopupMenu buildTrayMenu (const TrayController& controller)
{
    const auto m = controller.menu();
    const auto ui = controller.uiState();

    juce::PopupMenu menu;
    juce::PopupMenu::Item header;
    header.itemID = menuTitle;
    header.customComponent = new MenuHeader (TrayController::uiStateText (ui), trayStateColour (ui));
    menu.addItem (std::move (header));
    menu.addSeparator();

    auto item = [&menu] (int id, const juce::String& text, bool enabled, theme::Icon icon, juce::Colour colour)
    {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.isEnabled = enabled;
        i.image = theme::makeIcon (icon, colour);
        if (colour != theme::text)
            i.colour = colour;
        menu.addItem (std::move (i));
    };
    // The status is the pill in the header; both pause and resume are listed.
    item (menuPause, "Pausar monitoramento", m.pauseEnabled, theme::Icon::pause, theme::text);
    item (menuResume, "Retomar monitoramento", m.resumeEnabled, theme::Icon::play, theme::text);
    item (menuScan, "Verificar agora", m.scanEnabled, theme::Icon::refresh, theme::text);
    menu.addSeparator();
    item (menuOpen, "Abrir", true, theme::Icon::window, theme::text);
    menu.addSeparator();
    item (menuExit, "Encerrar", m.exitEnabled, theme::Icon::power, theme::danger);
    return menu;
}
} // namespace audioslave
