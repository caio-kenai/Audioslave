#pragma once
// The notification-area icon (juce::SystemTrayIconComponent): the Audioslave
// logo, grey while monitoring is paused or stopped, with the required
// tooltip. Left click opens the status window, right click the menu.
// JUCE re-adds the icon when Explorer restarts (TaskbarCreated).

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace audioslave
{
class TrayIcon final : public juce::SystemTrayIconComponent
{
public:
    std::function<void()> onOpen;
    std::function<void()> onMenu;

    TrayIcon();

    void setPaused (bool paused);

    // Size in pixels the shell uses for notification icons on this system.
    static int smallIconSize();
    // The logo (normal or grey) closest to `size`, from the original .ico frames.
    static juce::Image logoImage (bool paused, int size);

private:
    void mouseDown (const juce::MouseEvent& e) override;
    void applyIcon();

    bool paused_ = false;
    bool iconSet_ = false;
};
} // namespace audioslave
