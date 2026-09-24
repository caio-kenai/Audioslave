#include "app/TrayMenu.h"
#include "app/Theme.h"
#include "app/TrayIcon.h"
#include "common/Branding.h"

#include <memory>

namespace audioslave
{
namespace
{
constexpr int liveRefreshMs = 150;

// Menu header: logo, product name and the status pill. Clicking it opens
// the status window. Follows the live status while the menu is open.
class MenuHeader final : public juce::PopupMenu::CustomComponent, private juce::Timer
{
public:
    MenuHeader (const TrayController& snapshot, std::function<const TrayController*()> live)
        : juce::PopupMenu::CustomComponent (true), live_ (std::move (live))
    {
        logo_ = TrayIcon::logoImage (false, 128);
        read (snapshot);
        if (live_)
            startTimer (liveRefreshMs);
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
        // drawImage uses the opacity of the current fill, which a menu item
        // inherits from the menu window: without this the logo was drawn
        // fully transparent until hovering set an opaque colour.
        g.setOpacity (1.0f);
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
    void read (const TrayController& c)
    {
        const auto ui = c.uiState();
        status_ = TrayController::uiStateText (ui);
        colour_ = trayStateColour (ui);
    }

    void timerCallback() override
    {
        if (const auto* c = live_())
        {
            const auto before = status_;
            read (*c);
            if (status_ != before)
                repaint();
        }
    }

    std::function<const TrayController*()> live_;
    juce::String status_;
    juce::Colour colour_;
    juce::Image logo_;
};

// An action that runs without closing the menu. It is drawn exactly like a
// regular item (same LookAndFeel call) and re-reads its enabled state from
// the live controller, so e.g. "Pausar" greys out and "Retomar" lights up as
// soon as the service confirms the pause.
class InPlaceItem final : public juce::PopupMenu::CustomComponent, private juce::Timer
{
public:
    using EnabledFn = std::function<bool (const TrayController::MenuModel&)>;

    InPlaceItem (juce::String text, theme::Icon icon, const TrayController& snapshot, EnabledFn enabledFn,
                 std::function<void()> action, std::function<const TrayController*()> live)
        : juce::PopupMenu::CustomComponent (false),
          text_ (std::move (text)),
          icon_ (theme::makeIcon (icon, theme::text)),
          enabledFn_ (std::move (enabledFn)),
          action_ (std::move (action)),
          live_ (std::move (live))
    {
        enabled_ = enabledFn_ (snapshot.menu());
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        if (live_)
            startTimer (liveRefreshMs);
    }

    void getIdealSize (int& width, int& height) override
    {
        getLookAndFeel().getIdealPopupMenuItemSize (text_, false, -1, width, height);
    }

    void paint (juce::Graphics& g) override
    {
        const bool highlighted = enabled_ && (isItemHighlighted() || isMouseOver (true));
        getLookAndFeel().drawPopupMenuItem (g, getLocalBounds(), false, enabled_, highlighted, false, false, text_, {},
                                            icon_.get(), nullptr);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! enabled_ || ! getLocalBounds().contains (e.getPosition()) || ! action_)
            return;
        // Disable immediately (the controller turns busy until the reply);
        // the timer re-reads the real state.
        enabled_ = false;
        repaint();
        action_();
    }

private:
    void timerCallback() override
    {
        if (const auto* c = live_())
        {
            const bool now = enabledFn_ (c->menu());
            if (now != enabled_)
            {
                enabled_ = now;
                repaint();
            }
        }
    }

    juce::String text_;
    std::unique_ptr<juce::Drawable> icon_;
    EnabledFn enabledFn_;
    std::function<void()> action_;
    std::function<const TrayController*()> live_;
    bool enabled_ = false;
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

juce::PopupMenu buildTrayMenu (const TrayController& controller, const TrayMenuActions& actions)
{
    const auto m = controller.menu();

    juce::PopupMenu menu;
    juce::PopupMenu::Item header;
    header.itemID = menuTitle;
    header.customComponent = new MenuHeader (controller, actions.live);
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
    auto inPlace = [&] (int id, const juce::String& text, theme::Icon icon, InPlaceItem::EnabledFn enabled,
                        const std::function<void()>& action)
    {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.customComponent = new InPlaceItem (text, icon, controller, std::move (enabled), action, actions.live);
        menu.addItem (std::move (i));
    };

    // The status is the pill in the header; both pause and resume are listed.
    inPlace (menuPause, "Pausar monitoramento", theme::Icon::pause, [] (const auto& mm) { return mm.pauseEnabled; }, actions.pause);
    inPlace (menuResume, "Retomar monitoramento", theme::Icon::play, [] (const auto& mm) { return mm.resumeEnabled; }, actions.resume);
    inPlace (menuScan, "Verificar agora", theme::Icon::refresh, [] (const auto& mm) { return mm.scanEnabled; }, actions.scan);
    menu.addSeparator();
    item (menuOpen, "Abrir", true, theme::Icon::window, theme::text);
    menu.addSeparator();
    item (menuExit, "Encerrar", m.exitEnabled, theme::Icon::power, theme::danger);
    return menu;
}
} // namespace audioslave
