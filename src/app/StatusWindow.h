#pragma once
// "Abrir": status window of the tray (JUCE DocumentWindow). Shows the live
// state pushed by the service, both features, the last scan and every
// monitored endpoint, with the same actions as the tray menu.

#include "app/Theme.h"
#include "app/TrayController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace audioslave
{
class StatusWindow final : public juce::DocumentWindow
{
public:
    struct Actions
    {
        std::function<void()> pause, resume, scan, openLogs;
    };

    StatusWindow (Actions actions, std::function<void()> onClose);
    ~StatusWindow() override;

    // Refreshes every field from the controller.
    void update (const TrayController& controller);

    void closeButtonPressed() override;

private:
    class Content;
    theme::LookAndFeel lookAndFeel_;
    std::function<void()> onClose_;
};
} // namespace audioslave
