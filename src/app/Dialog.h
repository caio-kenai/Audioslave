#pragma once
// Audioslave's modal dialog: a borderless, rounded dark card drawn by JUCE
// (icon badge, title, message, right-aligned buttons, optional extra control
// such as a checkbox). Shared by the tray and the installer.

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace audioslave::theme
{
struct DialogOptions
{
    juce::MessageBoxIconType icon = juce::MessageBoxIconType::InfoIcon;
    juce::String title;
    juce::String message;
    juce::StringArray buttons { "OK" };   // the first one is the main action
    bool destructive = false;             // paint the main action red
    int width = 480;                      // compact dialogs (e.g. a single text field) use less
    std::unique_ptr<juce::Component> extra; // shown under the message (owned)
};

// Result: index of the button pressed (0 = first), -1 when dismissed with
// Esc. `extra` is still alive when the callback runs.
using DialogCallback = std::function<void (int result, juce::Component* extra)>;

void showDialog (DialogOptions options, DialogCallback callback = {});

// Convenience: one "OK" button.
void showMessage (juce::MessageBoxIconType icon, const juce::String& title, const juce::String& message,
                  std::function<void()> onClose = {});
} // namespace audioslave::theme
