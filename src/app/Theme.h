#pragma once
// Audioslave's visual language (JUCE LookAndFeel) shared by the tray menu,
// the status window, the dialogs and the installer: the logo's black
// surfaces with its orange as the single accent, Segoe UI typography,
// rounded controls, switch-style toggles and JUCE-drawn window chrome.

#include <juce_gui_basics/juce_gui_basics.h>

namespace audioslave::theme
{
// Palette -------------------------------------------------------------------
inline const juce::Colour background { 0xff0d0d0f };    // window (logo black)
inline const juce::Colour titleBar { 0xff09090b };
inline const juce::Colour surface { 0xff16161a };       // cards
inline const juce::Colour surfaceRaised { 0xff1f1f24 }; // hover / table stripes
inline const juce::Colour outline { 0xff2b2b31 };
inline const juce::Colour outlineStrong { 0xff3b3b43 };
inline const juce::Colour accent { 0xfffe6902 };        // logo orange
inline const juce::Colour accentHover { 0xffff7f24 };
inline const juce::Colour accentSoft { 0x33fe6902 };
inline const juce::Colour accentLight { 0xffffa55c };   // highlights, focus
inline const juce::Colour text { 0xfff4f4f6 };
inline const juce::Colour textDim { 0xffa6a6b0 };
inline const juce::Colour textFaint { 0xff6c6c76 };
inline const juce::Colour ok { 0xff34d399 };
inline const juce::Colour warning { 0xfffbbf24 };
inline const juce::Colour danger { 0xfff87171 };

inline constexpr float radius = 8.0f;

// Typography ----------------------------------------------------------------
juce::Font font (float height, bool bold = false);

// Button variants: button.getProperties().set (variantProperty, "primary" | "ghost").
inline const juce::Identifier variantProperty { "variant" };
void setVariant (juce::Button& button, const juce::String& variant);

// Paints a card (rounded surface with a hairline border).
void paintCard (juce::Graphics& g, juce::Rectangle<float> area);

// Paints a status pill: coloured dot + text on a tinted background.
void paintPill (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, const juce::String& label);

// Small line icons for menus and buttons.
enum class Icon
{
    pause,
    play,
    refresh,
    window,
    folder,
    power
};
std::unique_ptr<juce::Drawable> makeIcon (Icon icon, juce::Colour colour);

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    // Buttons
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    // Combo boxes / text editors
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    // Popup menus (tray menu, combo box lists)
    void drawPopupMenuBackgroundWithOptions (juce::Graphics&, int width, int height, const juce::PopupMenu::Options&) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth,
                                    int& idealHeight) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override { return 6; }
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;

    // Dialogs
    juce::AlertWindow* createAlertWindow (const juce::String& title, const juce::String& message, const juce::String& button1,
                                          const juce::String& button2, const juce::String& button3,
                                          juce::MessageBoxIconType iconType, int numButtons,
                                          juce::Component* associatedComponent) override;
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    int getAlertWindowButtonHeight() override { return 36; }
    juce::Font getAlertWindowTitleFont() override { return font (19.0f, true); }
    juce::Font getAlertWindowMessageFont() override { return font (15.0f); }
    juce::Font getAlertWindowFont() override { return font (15.0f); }

    // Window chrome (JUCE-drawn title bar)
    void drawDocumentWindowTitleBar (juce::DocumentWindow&, juce::Graphics&, int w, int h, int titleSpaceX, int titleSpaceW,
                                     const juce::Image* icon, bool drawTitleTextOnLeft) override;
    juce::Button* createDocumentWindowButton (int buttonType) override;
    void positionDocumentWindowButtons (juce::DocumentWindow&, int titleBarX, int titleBarY, int titleBarW, int titleBarH,
                                        juce::Button* minimiseButton, juce::Button* maximiseButton, juce::Button* closeButton,
                                        bool positionTitleBarButtonsOnLeft) override;

    // Tables, scroll bars, progress
    void drawTableHeaderBackground (juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& columnName, int columnId,
                                int width, int height, bool isMouseOver, bool isMouseDown, int columnFlags) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override { return 10; }
    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int width, int height, double progress,
                          const juce::String& textToShow) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
};
} // namespace audioslave::theme
