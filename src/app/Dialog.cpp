#include "app/Dialog.h"
#include "app/Theme.h"

namespace audioslave::theme
{
namespace
{
class TextField final : public juce::Component
{
public:
    TextField (const juce::String& initialText, std::function<juce::String (const juce::String&)> caption)
        : caption_ (std::move (caption))
    {
        editor.setFont (font (15.0f));
        editor.setIndents (12, 8);
        editor.setColour (juce::TextEditor::backgroundColourId, surface);
        editor.setText (initialText, false);
        editor.selectAll();
        editor.onTextChange = [this] { updateCaption(); };
        addAndMakeVisible (editor);
        if (caption_)
        {
            hint.setFont (font (13.0f));
            hint.setColour (juce::Label::textColourId, textDim);
            hint.setMinimumHorizontalScale (1.0f);
            addAndMakeVisible (hint);
            updateCaption();
        }
        setSize (100, caption_ ? 36 + 28 : 36);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        editor.setBounds (r.removeFromTop (36));
        hint.setBounds (r.withTrimmedTop (6));
    }

    juce::TextEditor editor;

private:
    void updateCaption()
    {
        if (caption_)
            hint.setText (caption_ (editor.getText().trim()), juce::dontSendNotification);
    }

    std::function<juce::String (const juce::String&)> caption_;
    juce::Label hint;
};

constexpr int padding = 26;
constexpr int iconSize = 40;
constexpr float corner = 14.0f;

class ModalDialog final : public juce::Component
{
public:
    ModalDialog (DialogOptions options, DialogCallback callback)
        : options_ (std::move (options)), callback_ (std::move (callback))
    {
        setOpaque (false);
        for (int i = 0; i < options_.buttons.size(); ++i)
        {
            auto* b = buttons_.add (new juce::TextButton (options_.buttons[i]));
            setVariant (*b, i == 0 ? (options_.destructive ? "danger" : "primary") : "secondary");
            b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
            b->onClick = [this, i] { finish (i); };
            addAndMakeVisible (b);
        }
        if (options_.extra != nullptr)
        {
            addAndMakeVisible (*options_.extra);
            if (auto* field = dynamic_cast<TextField*> (options_.extra.get()))
            {
                field_ = &field->editor;
                field_->onReturnKey = [this] { finish (0); };
                field_->onEscapeKey = [this] { finish (-1); };
            }
        }

        layoutText();
        const int extraH = options_.extra != nullptr ? options_.extra->getHeight() + 14 : 0;
        const int h = padding + (int) std::ceil (text_.getHeight()) + 24 + extraH + 40 + padding;
        setSize (options_.width, juce::jmax (150, h));
        setWantsKeyboardFocus (true);
    }

    void show()
    {
        addToDesktop (juce::ComponentPeer::windowHasDropShadow | juce::ComponentPeer::windowAppearsOnTaskbar);
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            setCentrePosition (display->userBounds.getCentre().roundToInt());
        setAlwaysOnTop (true);
        setVisible (true);
        toFront (true);
        juce::Process::makeForegroundProcess();
        enterModalState (true, nullptr, true);
        if (field_ != nullptr)
            field_->grabKeyboardFocus();
        else
            grabKeyboardFocus();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (background);
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (outlineStrong);
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

        // Icon badge.
        juce::Colour colour = accent;
        juce::String glyph = "i";
        if (options_.icon == juce::MessageBoxIconType::WarningIcon)
        {
            colour = options_.destructive ? danger : warning;
            glyph = "!";
        }
        else if (options_.icon == juce::MessageBoxIconType::QuestionIcon)
        {
            glyph = "?";
        }
        if (options_.icon != juce::MessageBoxIconType::NoIcon)
        {
            const auto badge = juce::Rectangle<float> ((float) padding, (float) padding, (float) iconSize, (float) iconSize);
            g.setColour (colour.withAlpha (0.16f));
            g.fillRoundedRectangle (badge, 10.0f);
            g.setColour (colour);
            g.setFont (font (20.0f, true));
            g.drawText (glyph, badge, juce::Justification::centred, false);
        }
        text_.draw (g, textArea_.toFloat());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (padding);
        // Right-aligned; left-to-right order follows the list (main action first).
        auto buttons = area.removeFromBottom (40);
        int total = 0;
        juce::Array<int> widths;
        for (auto* b : buttons_)
        {
            const int w = juce::jmax (110, juce::GlyphArrangement::getStringWidthInt (font (15.0f, true), b->getButtonText()) + 40);
            widths.add (w);
            total += w;
        }
        total += 10 * (buttons_.size() - 1);
        int x = buttons.getRight() - total;
        for (int i = 0; i < buttons_.size(); ++i)
        {
            buttons_[i]->setBounds (x, buttons.getY(), widths[i], buttons.getHeight());
            x += widths[i] + 10;
        }

        if (options_.icon != juce::MessageBoxIconType::NoIcon)
            area.removeFromLeft (iconSize + 16);
        textArea_ = area.removeFromTop ((int) std::ceil (text_.getHeight()) + 2);
        if (options_.extra != nullptr)
        {
            area.removeFromTop (14);
            options_.extra->setBounds (area.removeFromTop (options_.extra->getHeight()));
        }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            finish (-1);
            return true;
        }
        if (key == juce::KeyPress::returnKey)
        {
            finish (0);
            return true;
        }
        return false;
    }

    void inputAttemptWhenModal() override { toFront (true); }

private:
    void layoutText()
    {
        juce::AttributedString s;
        s.setWordWrap (juce::AttributedString::byWord);
        s.append (options_.title, font (18.0f, true), text);
        if (options_.message.isNotEmpty())
        {
            s.append ("\n", font (10.0f), text);
            s.append (options_.message, font (14.5f), textDim);
        }
        s.setLineSpacing (3.0f);
        const int textWidth = options_.width - 2 * padding - (options_.icon != juce::MessageBoxIconType::NoIcon ? iconSize + 16 : 0);
        text_.createLayout (s, (float) textWidth);
    }

    void finish (int result)
    {
        if (finished_)
            return;
        finished_ = true;
        auto callback = std::move (callback_);
        if (callback)
            callback (result, options_.extra.get());
        exitModalState (result);
        // deleteWhenDismissed = true: JUCE deletes this component.
    }

    DialogOptions options_;
    DialogCallback callback_;
    juce::OwnedArray<juce::TextButton> buttons_;
    juce::TextLayout text_;
    juce::Rectangle<int> textArea_;
    juce::TextEditor* field_ = nullptr;
    bool finished_ = false;
};
} // namespace

void showDialog (DialogOptions options, DialogCallback callback)
{
    auto* dialog = new ModalDialog (std::move (options), std::move (callback));
    dialog->show();
}

std::unique_ptr<juce::Component> makeTextField (const juce::String& initialText,
                                                std::function<juce::String (const juce::String&)> caption)
{
    return std::make_unique<TextField> (initialText, std::move (caption));
}

juce::String textFieldValue (juce::Component* extra)
{
    if (auto* field = dynamic_cast<TextField*> (extra))
        return field->editor.getText().trim();
    return {};
}

void showMessage (juce::MessageBoxIconType icon, const juce::String& title, const juce::String& message,
                  std::function<void()> onClose)
{
    DialogOptions o;
    o.icon = icon;
    o.title = title;
    o.message = message;
    showDialog (std::move (o), [onClose = std::move (onClose)] (int, juce::Component*)
    {
        if (onClose)
            onClose();
    });
}
} // namespace audioslave::theme
