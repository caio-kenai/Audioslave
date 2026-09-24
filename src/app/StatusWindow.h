#pragma once
// "Abrir": the tray's status window, drawn entirely by JUCE (own title bar,
// dark theme). Shows the live state pushed by the service, both features,
// activity, every monitored endpoint and the same actions as the tray menu.

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
    bool keyPressed (const juce::KeyPress& key) override;

private:
    class Content;
    std::function<void()> onClose_;
};
} // namespace audioslave
