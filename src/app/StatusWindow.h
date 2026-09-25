#pragma once
// "Abrir": the tray's status window, drawn entirely by JUCE (own title bar,
// dark theme). Shows the live state pushed by the service, both features,
// activity, every monitored endpoint and the same actions as the tray menu.
//
// A regular Windows window: minimise, maximise / restore (also by
// double-clicking the title bar or with snap layouts) and close buttons, plus
// resizing from the edges. Its position, size and maximised state are
// remembered per user (ui.settings in the user's AppData folder).

#include "app/SettingsView.h"
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
        // Sends a command with arguments to the service (reply on the message thread).
        SettingsView::Request request;
    };

    StatusWindow (Actions actions, std::function<void()> onClose);
    ~StatusWindow() override;

    // Refreshes every field from the controller.
    void update (const TrayController& controller);

    // Shows the "Configurações" page (or the dashboard).
    void showSettings (bool show);

    // Shows the window in front of every other application, restored if it
    // was minimised, like launching a program does.
    void bringToFront();

    enum DeviceMenuItem
    {
        deviceRename = 1,
        deviceReleaseName,
        deviceEnable
    };
    // Right-click menu of a device row.
    static juce::PopupMenu deviceMenu (const EndpointStatus& device, bool connected);

    void closeButtonPressed() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void resized() override;

private:
    class Content;
    void saveWindowState();
    std::function<void()> onClose_;
};
} // namespace audioslave
