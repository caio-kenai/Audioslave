#include "app/Theme.h"

namespace audioslave::theme
{
namespace
{
constexpr float popupRadius = 10.0f;

juce::String variantOf (const juce::Component& c)
{
    return c.getProperties().getWithDefault (variantProperty, "secondary").toString();
}

// Title-bar buttons (minimise / maximise-restore / close) drawn as thin
// glyphs. They are the DocumentWindow's own buttons, so they drive the real
// window (ShowWindow minimise / maximise / restore, Windows 11 snap layouts).
class TitleBarButton final : public juce::Button
{
public:
    enum class Kind
    {
        minimise,
        maximise,
        close
    };

    TitleBarButton (const juce::String& name, Kind kind) : juce::Button (name), kind_ (kind) {}

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const bool isClose = kind_ == Kind::close;
        if (highlighted || down)
        {
            g.setColour (isClose ? danger.withAlpha (down ? 0.9f : 0.75f) : surfaceRaised.brighter (down ? 0.2f : 0.0f));
            g.fillRect (getLocalBounds());
        }
        const auto c = getLocalBounds().toFloat().getCentre();
        const float s = 5.0f;
        g.setColour (highlighted && isClose ? juce::Colours::white : textDim);
        juce::Path p;
        switch (kind_)
        {
            case Kind::close:
                p.startNewSubPath (c.x - s, c.y - s);
                p.lineTo (c.x + s, c.y + s);
                p.startNewSubPath (c.x + s, c.y - s);
                p.lineTo (c.x - s, c.y + s);
                break;
            case Kind::minimise:
                p.startNewSubPath (c.x - s, c.y);
                p.lineTo (c.x + s, c.y);
                break;
            case Kind::maximise:
                if (isMaximised())
                {
                    // Restore: two overlapping frames.
                    p.addRoundedRectangle (c.x - s, c.y - s + 2.0f, 2.0f * s - 2.0f, 2.0f * s - 2.0f, 1.2f);
                    p.startNewSubPath (c.x - s + 2.0f, c.y - s);
                    p.lineTo (c.x + s, c.y - s);
                    p.lineTo (c.x + s, c.y + s - 2.0f);
                }
                else
                {
                    p.addRoundedRectangle (c.x - s, c.y - s, 2.0f * s, 2.0f * s, 1.2f);
                }
                break;
        }
        g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    bool isMaximised() const
    {
        const auto* window = findParentComponentOfClass<juce::DocumentWindow>();
        return window != nullptr && window->isFullScreen();
    }

    Kind kind_;
};

void iconForMessageType (juce::Graphics& g, juce::Rectangle<float> area, juce::MessageBoxIconType type)
{
    juce::Colour colour = accent;
    juce::String glyph = "i";
    if (type == juce::MessageBoxIconType::WarningIcon)
    {
        colour = warning;
        glyph = "!";
    }
    else if (type == juce::MessageBoxIconType::QuestionIcon)
    {
        colour = accent;
        glyph = "?";
    }
    g.setColour (colour.withAlpha (0.16f));
    g.fillEllipse (area);
    g.setColour (colour);
    g.drawEllipse (area.reduced (1.0f), 1.5f);
    g.setFont (font (area.getHeight() * 0.5f, true));
    g.drawText (glyph, area, juce::Justification::centred, false);
}
} // namespace

juce::Font font (float height, bool bold)
{
    return juce::FontOptions ("Segoe UI", height, bold ? juce::Font::bold : juce::Font::plain);
}

void setVariant (juce::Button& button, const juce::String& variant)
{
    button.getProperties().set (variantProperty, variant);
    button.repaint();
}

const juce::Identifier iconProperty { "icon" };

void setButtonIcon (juce::Button& button, Icon icon)
{
    button.getProperties().set (iconProperty, static_cast<int> (icon));
    button.repaint();
}

void paintCard (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (surface);
    g.fillRoundedRectangle (area, radius + 2.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (area.reduced (0.5f), radius + 2.0f, 1.0f);
}

void paintPill (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, const juce::String& label)
{
    g.setColour (colour.withAlpha (0.14f));
    g.fillRoundedRectangle (area, area.getHeight() * 0.5f);
    g.setColour (colour.withAlpha (0.45f));
    g.drawRoundedRectangle (area.reduced (0.5f), area.getHeight() * 0.5f, 1.0f);
    const float dot = area.getHeight() * 0.32f;
    const auto dotArea = juce::Rectangle<float> (dot, dot).withCentre ({ area.getX() + area.getHeight() * 0.55f, area.getCentreY() });
    g.setColour (colour.withAlpha (0.35f));
    g.fillEllipse (dotArea.expanded (dot * 0.45f));
    g.setColour (colour);
    g.fillEllipse (dotArea);
    g.setFont (font (area.getHeight() * 0.48f, true));
    g.drawText (label, area.withTrimmedLeft (area.getHeight() * 0.95f).withTrimmedRight (area.getHeight() * 0.4f),
                juce::Justification::centredLeft, false);
}

std::unique_ptr<juce::Drawable> makeIcon (Icon icon, juce::Colour colour)
{
    juce::Path p;
    bool filled = false;
    switch (icon)
    {
        case Icon::pause:
            p.addRoundedRectangle (5.5f, 4.5f, 3.2f, 11.0f, 1.2f);
            p.addRoundedRectangle (11.3f, 4.5f, 3.2f, 11.0f, 1.2f);
            filled = true;
            break;
        case Icon::play:
            p.addTriangle (6.5f, 4.5f, 6.5f, 15.5f, 15.5f, 10.0f);
            filled = true;
            break;
        case Icon::refresh:
            p.addCentredArc (10.0f, 10.0f, 6.0f, 6.0f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.2f, true);
            p.startNewSubPath (13.5f, 2.8f);
            p.lineTo (15.2f, 5.6f);
            p.lineTo (12.0f, 6.4f);
            break;
        case Icon::window:
            p.addRoundedRectangle (3.0f, 4.0f, 14.0f, 12.0f, 2.0f);
            p.startNewSubPath (3.0f, 7.5f);
            p.lineTo (17.0f, 7.5f);
            break;
        case Icon::folder:
            p.startNewSubPath (3.0f, 6.0f);
            p.lineTo (3.0f, 15.0f);
            p.lineTo (17.0f, 15.0f);
            p.lineTo (17.0f, 7.0f);
            p.lineTo (9.5f, 7.0f);
            p.lineTo (8.0f, 5.0f);
            p.lineTo (3.0f, 5.0f);
            p.closeSubPath();
            break;
        case Icon::power:
            p.addCentredArc (10.0f, 10.5f, 6.0f, 6.0f, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
            p.startNewSubPath (10.0f, 3.0f);
            p.lineTo (10.0f, 9.5f);
            break;
        case Icon::edit:
            // Pencil.
            p.startNewSubPath (4.5f, 15.5f);
            p.lineTo (5.2f, 12.2f);
            p.lineTo (13.2f, 4.2f);
            p.lineTo (15.8f, 6.8f);
            p.lineTo (7.8f, 14.8f);
            p.closeSubPath();
            p.startNewSubPath (11.6f, 5.8f);
            p.lineTo (14.2f, 8.4f);
            break;
        case Icon::settings:
            // Three sliders.
            for (const auto [y, knob] : { std::pair (5.0f, 13.0f), std::pair (10.0f, 7.0f), std::pair (15.0f, 11.0f) })
            {
                p.startNewSubPath (3.0f, y);
                p.lineTo (17.0f, y);
                p.addEllipse (knob - 1.8f, y - 1.8f, 3.6f, 3.6f);
            }
            break;
    }
    // Same 20 x 20 frame for every icon, so they all scale alike.
    p.startNewSubPath (0.0f, 0.0f);
    p.startNewSubPath (20.0f, 20.0f);
    auto drawable = std::make_unique<juce::DrawablePath>();
    drawable->setPath (p);
    if (filled)
    {
        drawable->setFill (colour);
    }
    else
    {
        drawable->setFill (juce::Colours::transparentBlack);
        drawable->setStrokeFill (colour);
        drawable->setStrokeType (juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    return drawable;
}

//==============================================================================
LookAndFeel::LookAndFeel()
    : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme { background, background, surface, outline, text, accent,
                                                                 juce::Colours::white, surface, text })
{
    setDefaultSansSerifTypefaceName ("Segoe UI");
    setUsingNativeAlertWindows (false);

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::textColourId, text);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, surfaceRaised);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, surface);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::ComboBox::focusedOutlineColourId, accent);
    // Slightly translucent so the menu window is created transparent and its
    // rounded corners show; the item area itself is painted fully opaque.
    setColour (juce::PopupMenu::backgroundColourId, surface.withAlpha (0.99f));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, textFaint);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ToggleButton::tickDisabledColourId, textFaint);
    setColour (juce::TextEditor::backgroundColourId, background);
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, accentLight);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TableHeaderComponent::backgroundColourId, surface);
    setColour (juce::TableHeaderComponent::textColourId, textFaint);
    setColour (juce::TableHeaderComponent::outlineColourId, outline);
    setColour (juce::ProgressBar::backgroundColourId, surfaceRaised);
    setColour (juce::ProgressBar::foregroundColourId, accent);
    setColour (juce::ScrollBar::thumbColourId, outlineStrong);
    setColour (juce::AlertWindow::backgroundColourId, background);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, outline);
    setColour (juce::TooltipWindow::backgroundColourId, surfaceRaised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, outline);
}

//==============================================================================
void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto variant = variantOf (button);
    const bool enabled = button.isEnabled();

    juce::Colour fill, border;
    if (variant == "primary")
    {
        fill = down ? accent.darker (0.15f) : highlighted ? accentHover : accent;
        border = fill;
    }
    else if (variant == "danger")
    {
        fill = down ? danger.darker (0.2f) : highlighted ? danger.brighter (0.1f) : danger.withAlpha (0.85f);
        border = fill;
    }
    else if (variant == "outline")
    {
        fill = down ? accent.withAlpha (0.28f) : highlighted ? accentSoft : accent.withAlpha (0.08f);
        border = highlighted ? accentHover : accent;
    }
    else if (variant == "ghost")
    {
        fill = down ? surfaceRaised.brighter (0.1f) : highlighted ? surfaceRaised : juce::Colours::transparentBlack;
        border = juce::Colours::transparentBlack;
    }
    else
    {
        fill = down ? surfaceRaised.brighter (0.15f) : highlighted ? surfaceRaised.brighter (0.07f) : surfaceRaised;
        border = highlighted ? outlineStrong : outline;
    }
    if (! enabled)
    {
        fill = fill.withMultipliedAlpha (0.45f);
        border = border.withMultipliedAlpha (0.45f);
    }
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (border);
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    if (button.hasKeyboardFocus (true) && enabled)
    {
        g.setColour (accentLight.withAlpha (0.6f));
        g.drawRoundedRectangle (bounds.reduced (1.5f), radius - 1.5f, 1.2f);
    }
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return font (juce::jmin (15.0f, (float) buttonHeight * 0.44f), true);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto variant = variantOf (button);
    auto colour = variant == "primary" || variant == "danger" ? juce::Colours::white
                  : variant == "ghost"                        ? textDim
                  : variant == "outline"                      ? accentLight
                                                              : text;
    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.45f);
    const auto f = getTextButtonFont (button, button.getHeight());
    auto area = button.getLocalBounds().reduced (12, 0);

    // Optional icon, centred together with the text.
    if (const auto* iconValue = button.getProperties().getVarPointer (iconProperty))
    {
        const float iconSize = 18.0f, gap = 8.0f;
        const float textW = juce::jmin ((float) area.getWidth() - iconSize - gap,
                                        juce::GlyphArrangement::getStringWidth (f, button.getButtonText()));
        const float x = (float) area.getCentreX() - (iconSize + gap + textW) * 0.5f;
        const auto icon = makeIcon (static_cast<Icon> (static_cast<int> (*iconValue)), colour);
        icon->drawWithin (g, { x, (float) area.getCentreY() - iconSize * 0.5f, iconSize, iconSize }, juce::RectanglePlacement::centred,
                          1.0f);
        area = juce::Rectangle<float> (x + iconSize + gap, (float) area.getY(), textW + 2.0f, (float) area.getHeight()).toNearestInt();
    }
    g.setColour (colour);
    g.setFont (f);
    g.drawFittedText (button.getButtonText(), area, juce::Justification::centred, 1);
}

// Switch-style toggles (track + knob) with the label on the right.
void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const bool on = button.getToggleState();
    const bool enabled = button.isEnabled();
    const float h = 20.0f, w = 36.0f;
    const auto track = juce::Rectangle<float> (w, h).withPosition (1.0f, ((float) button.getHeight() - h) * 0.5f);

    auto trackColour = on ? accent : surfaceRaised.brighter (highlighted ? 0.12f : 0.0f);
    if (! enabled)
        trackColour = trackColour.withMultipliedAlpha (0.5f);
    g.setColour (trackColour);
    g.fillRoundedRectangle (track, h * 0.5f);
    g.setColour (on ? trackColour : outlineStrong);
    g.drawRoundedRectangle (track.reduced (0.5f), h * 0.5f, 1.0f);

    const float knob = h - 6.0f;
    const auto knobArea = juce::Rectangle<float> (knob, knob)
                              .withCentre ({ on ? track.getRight() - h * 0.5f : track.getX() + h * 0.5f, track.getCentreY() });
    g.setColour (on ? juce::Colours::white : textDim);
    if (! enabled)
        g.setColour ((on ? juce::Colours::white : textDim).withMultipliedAlpha (0.6f));
    g.fillEllipse (knobArea);

    g.setColour (enabled ? text : textDim);
    g.setFont (font (15.0f));
    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().withTrimmedLeft ((int) w + 12), juce::Justification::centredLeft, 2);
}

//==============================================================================
void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    const bool enabled = box.isEnabled();
    g.setColour (enabled ? background : background.withMultipliedAlpha (0.6f));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (box.hasKeyboardFocus (true) ? accent : (box.isMouseOver (true) && enabled ? outlineStrong : outline));
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    juce::Path chevron;
    const float cx = (float) width - 16.0f, cy = (float) height * 0.5f;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (enabled ? textDim : textFaint);
    g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return font (15.0f);
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height), radius);
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;
    g.setColour (editor.hasKeyboardFocus (true) ? accent : outline);
    g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f), radius, 1.0f);
}

//==============================================================================
void LookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height, const juce::PopupMenu::Options&)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (surface);
    g.fillRoundedRectangle (bounds, popupRadius);
    g.setColour (outlineStrong);
    g.drawRoundedRectangle (bounds.reduced (0.5f), popupRadius, 1.0f);
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                     bool isHighlighted, bool isTicked, bool, const juce::String& itemText,
                                     const juce::String& shortcutKeyText, const juce::Drawable* icon,
                                     const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (outline);
        g.fillRect (area.reduced (10, 0).withHeight (1).withCentre (area.getCentre()));
        return;
    }

    auto r = area.reduced (5, 1).toFloat();
    if (isHighlighted && isActive)
    {
        g.setColour (accent);
        g.fillRoundedRectangle (r, 6.0f);
    }

    auto colour = textColour != nullptr ? *textColour : text;
    if (isHighlighted && isActive)
        colour = juce::Colours::white;
    if (! isActive)
        colour = textFaint;

    auto content = r.reduced (10.0f, 0.0f);
    auto iconArea = content.removeFromLeft (20.0f).withSizeKeepingCentre (18.0f, 18.0f);
    if (icon != nullptr)
    {
        auto copy = icon->createCopy();
        copy->replaceColour (text, colour);
        copy->replaceColour (danger, isHighlighted && isActive ? juce::Colours::white : (isActive ? danger : textFaint));
        copy->replaceColour (textDim, colour);
        copy->drawWithin (g, iconArea, juce::RectanglePlacement::centred, isActive ? 1.0f : 0.5f);
    }
    else if (isTicked)
    {
        g.setColour (colour);
        g.fillEllipse (iconArea.withSizeKeepingCentre (7.0f, 7.0f));
    }
    content.removeFromLeft (10.0f);

    g.setColour (colour);
    g.setFont (getPopupMenuFont());
    g.drawFittedText (itemText, content.toNearestInt(), juce::Justification::centredLeft, 1);
    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (textDim);
        g.drawText (shortcutKeyText, content, juce::Justification::centredRight, true);
    }
}

void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& itemText, bool isSeparator, int, int& idealWidth,
                                             int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = 11;
        return;
    }
    idealHeight = 34;
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (getPopupMenuFont(), itemText, 0.0f, 0.0f);
    idealWidth = juce::jmax (220, (int) glyphs.getBoundingBox (0, -1, true).getWidth() + 70);
}

juce::Font LookAndFeel::getPopupMenuFont()
{
    return font (14.5f);
}

void LookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    g.setColour (textFaint);
    g.setFont (font (12.0f, true));
    g.drawFittedText (sectionName.toUpperCase(), area.reduced (15, 0), juce::Justification::bottomLeft, 1);
}

//==============================================================================
juce::AlertWindow* LookAndFeel::createAlertWindow (const juce::String& title, const juce::String& message,
                                                   const juce::String& button1, const juce::String& button2,
                                                   const juce::String& button3, juce::MessageBoxIconType iconType,
                                                   int numButtons, juce::Component* associatedComponent)
{
    auto* alert = juce::LookAndFeel_V4::createAlertWindow (title, message, button1, button2, button3, iconType, numButtons,
                                                          associatedComponent);
    // The first button is the main action.
    for (int i = 0; i < alert->getNumButtons(); ++i)
        if (auto* b = alert->getButton (i))
            setVariant (*b, i == 0 ? "primary" : "secondary");
    return alert;
}

void LookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea,
                                juce::TextLayout& textLayout)
{
    const auto bounds = alert.getLocalBounds().toFloat();
    g.setColour (background);
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (outlineStrong);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 12.0f, 1.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (bounds.withHeight (3.0f).reduced (12.0f, 0.0f), 1.5f);

    if (alert.getAlertType() != juce::MessageBoxIconType::NoIcon && textArea.getX() >= 56)
        iconForMessageType (g, juce::Rectangle<float> (18.0f, (float) textArea.getY() + 2.0f, 34.0f, 34.0f), alert.getAlertType());

    g.setColour (text);
    textLayout.draw (g, textArea.toFloat());
}

//==============================================================================
void LookAndFeel::drawDocumentWindowTitleBar (juce::DocumentWindow& window, juce::Graphics& g, int w, int h, int, int,
                                              const juce::Image* icon, bool)
{
    g.setColour (titleBar);
    g.fillAll();
    g.setColour (outline);
    g.fillRect (0, h - 1, w, 1);

    int x = 14;
    if (icon != nullptr && icon->isValid())
    {
        const int size = juce::jmin (18, h - 12);
        g.drawImage (*icon, juce::Rectangle<float> ((float) x, (float) (h - size) * 0.5f, (float) size, (float) size),
                     juce::RectanglePlacement::centred);
        x += size + 10;
    }
    g.setColour (window.isActiveWindow() ? text : textDim);
    g.setFont (font (14.0f, true));
    g.drawText (window.getName(), x, 0, w - x - 100, h, juce::Justification::centredLeft, true);
}

juce::Button* LookAndFeel::createDocumentWindowButton (int buttonType)
{
    if (buttonType == juce::DocumentWindow::closeButton)
        return new TitleBarButton ("Fechar", TitleBarButton::Kind::close);
    if (buttonType == juce::DocumentWindow::minimiseButton)
        return new TitleBarButton ("Minimizar", TitleBarButton::Kind::minimise);
    if (buttonType == juce::DocumentWindow::maximiseButton)
        return new TitleBarButton ("Maximizar", TitleBarButton::Kind::maximise);
    return juce::LookAndFeel_V4::createDocumentWindowButton (buttonType);
}

void LookAndFeel::positionDocumentWindowButtons (juce::DocumentWindow&, int titleBarX, int titleBarY, int titleBarW,
                                                 int titleBarH, juce::Button* minimiseButton, juce::Button* maximiseButton,
                                                 juce::Button* closeButton, bool)
{
    const int buttonW = 46;
    int x = titleBarX + titleBarW - buttonW;
    for (auto* b : { closeButton, maximiseButton, minimiseButton })
    {
        if (b == nullptr)
            continue;
        b->setBounds (x, titleBarY, buttonW, titleBarH - 1);
        x -= buttonW;
    }
}

//==============================================================================
void LookAndFeel::drawTableHeaderBackground (juce::Graphics& g, juce::TableHeaderComponent& header)
{
    g.setColour (surface);
    g.fillRect (header.getLocalBounds());
    g.setColour (outline);
    g.fillRect (header.getLocalBounds().removeFromBottom (1));
}

void LookAndFeel::drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent&, const juce::String& columnName, int,
                                         int width, int height, bool, bool, int)
{
    g.setColour (textFaint);
    g.setFont (font (12.0f, true));
    g.drawFittedText (columnName.toUpperCase(), juce::Rectangle<int> (width, height).reduced (10, 0),
                      juce::Justification::centredLeft, 1);
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                 int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    juce::Rectangle<float> thumb;
    if (vertical)
        thumb = { (float) x + (float) width * 0.3f, (float) thumbStart, (float) width * 0.4f, (float) thumbSize };
    else
        thumb = { (float) thumbStart, (float) y + (float) height * 0.3f, (float) thumbSize, (float) height * 0.4f };
    g.setColour (isMouseDown ? textDim : isMouseOver ? outlineStrong.brighter (0.2f) : outlineStrong);
    g.fillRoundedRectangle (thumb, juce::jmin (thumb.getWidth(), thumb.getHeight()) * 0.5f);
}

void LookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar&, int width, int height, double progress,
                                   const juce::String&)
{
    const auto track = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (surfaceRaised);
    g.fillRoundedRectangle (track, (float) height * 0.5f);
    if (progress >= 0.0 && progress <= 1.0)
    {
        const auto bar = track.withWidth (juce::jmax ((float) height, track.getWidth() * (float) progress));
        g.setGradientFill (juce::ColourGradient (accent, bar.getX(), 0.0f, accentLight, bar.getRight(), 0.0f, false));
        g.fillRoundedRectangle (bar, (float) height * 0.5f);
    }
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& tipText, int width, int height)
{
    const auto bounds = juce::Rectangle<float> ((float) width, (float) height);
    g.setColour (surfaceRaised);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (text);
    g.setFont (font (13.5f));
    g.drawFittedText (tipText, bounds.reduced (8.0f, 4.0f).toNearestInt(), juce::Justification::centredLeft, 3);
}
} // namespace audioslave::theme
