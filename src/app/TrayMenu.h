#pragma once
// The tray's context menu (juce::PopupMenu, dark theme): a header with the
// logo, the product name and the live status pill, then the actions.

#include "app/TrayController.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace audioslave
{
enum TrayMenuItem
{
    menuTitle = 1,
    menuPause,
    menuResume,
    menuScan,
    menuOpen,
    menuExit
};

juce::PopupMenu buildTrayMenu (const TrayController& controller);

// Status colour used by the tray (pill, window).
juce::Colour trayStateColour (TrayController::UiState state);
} // namespace audioslave
