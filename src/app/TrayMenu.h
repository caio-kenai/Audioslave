#pragma once
// The tray's context menu (juce::PopupMenu, dark theme): a header with the
// logo, the product name and the live status pill, then the actions.
//
// Pause, resume and "Verificar agora" run in place and keep the menu open
// (custom items that are not triggered automatically), so several actions can
// be taken in one go; the header, the pill and every item's enabled state
// follow the live status while the menu is shown. Opening the window or
// exiting closes the menu (their ids are returned by the menu).

#include "app/TrayController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

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

struct TrayMenuActions
{
    // In-place actions (the menu stays open).
    std::function<void()> pause, resume, scan;
    // The current controller, or nullptr once the application is going away.
    // Empty: the menu is a static snapshot of the controller passed in.
    std::function<const TrayController*()> live;
};

juce::PopupMenu buildTrayMenu (const TrayController& controller, const TrayMenuActions& actions = {});

// Status colour used by the tray (pill, window).
juce::Colour trayStateColour (TrayController::UiState state);
} // namespace audioslave
