#pragma once
// The notification-area icon (juce::SystemTrayIconComponent): the Audioslave
// logo, grey while monitoring is paused or stopped, with the required
// tooltip. Left click opens the status window, right click the menu.
// JUCE re-adds the icon when Explorer restarts (TaskbarCreated).
//
// JUCE drives the icon with the legacy (version 0) notification protocol and
// takes the foreground on the right button's *down* event. On Windows 11 that
// deactivates the hidden-icons flyout while the button is still pressed, so
// the flyout closes and the icon vanishes while its menu is open. The icon
// therefore switches to NOTIFYICON_VERSION_4 and handles the shell's own
// events (NIN_SELECT, WM_CONTEXTMENU, sent when the click completes), exactly
// like a native Windows tray application.

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace audioslave
{
class TrayIcon final : public juce::SystemTrayIconComponent
{
public:
    std::function<void()> onOpen;
    // `iconArea`: the icon's screen rectangle (logical pixels), empty when the
    // shell cannot report it; the menu is placed next to it.
    std::function<void (juce::Rectangle<int> iconArea)> onMenu;

    TrayIcon();
    ~TrayIcon() override;

    void setPaused (bool paused);

    // Size in pixels the shell uses for notification icons on this system.
    static int smallIconSize();
    // The logo (normal or grey) closest to `size`, from the original .ico frames.
    static juce::Image logoImage (bool paused, int size);

private:
    struct Native; // window subclass (TrayIcon.cpp)
    friend struct Native;

    void applyIcon();
    void useModernProtocol();
    juce::Rectangle<int> iconScreenArea() const;

    bool paused_ = false;
    bool iconSet_ = false;
    void* subclassedWindow_ = nullptr;
};
} // namespace audioslave
