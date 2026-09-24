#pragma once
// Colours and LookAndFeel shared by the tray window and the installer,
// taken from the logo (blue gradient on dark navy).

#include <juce_gui_basics/juce_gui_basics.h>

namespace audioslave::theme
{
inline const juce::Colour background { 0xff0e1a33 };   // dark navy
inline const juce::Colour surface { 0xff15254a };      // cards / lists
inline const juce::Colour surfaceAlt { 0xff1a2d57 };
inline const juce::Colour outline { 0xff2a4478 };
inline const juce::Colour accent { 0xff1f7bff };       // logo blue
inline const juce::Colour accentBright { 0xff4fb3ff }; // logo cyan
inline const juce::Colour text { 0xffeef3ff };
inline const juce::Colour textDim { 0xff9fb0d6 };
inline const juce::Colour ok { 0xff3ddc97 };
inline const juce::Colour warning { 0xffffc857 };
inline const juce::Colour danger { 0xffff6b6b };

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
        : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme {
              background, surface, surfaceAlt, outline, text, accent, text, accent, text })
    {
        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::DocumentWindow::textColourId, text);
        setColour (juce::Label::textColourId, text);
        setColour (juce::TextButton::buttonColourId, surfaceAlt);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, text);
        setColour (juce::ComboBox::backgroundColourId, surface);
        setColour (juce::ComboBox::outlineColourId, outline);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, accentBright);
        setColour (juce::PopupMenu::backgroundColourId, surface);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::PopupMenu::highlightedTextColourId, text);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accentBright);
        setColour (juce::ToggleButton::tickDisabledColourId, textDim);
        setColour (juce::TextEditor::backgroundColourId, surface);
        setColour (juce::TextEditor::outlineColourId, outline);
        setColour (juce::TextEditor::focusedOutlineColourId, accentBright);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::ListBox::backgroundColourId, surface);
        setColour (juce::ListBox::outlineColourId, outline);
        setColour (juce::ProgressBar::backgroundColourId, surface);
        setColour (juce::ProgressBar::foregroundColourId, accent);
        setColour (juce::ScrollBar::thumbColourId, outline);
        setColour (juce::AlertWindow::backgroundColourId, background);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::AlertWindow::outlineColourId, outline);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::FontOptions (juce::jmin (15.0f, (float) buttonHeight * 0.5f));
    }
};
} // namespace audioslave::theme
