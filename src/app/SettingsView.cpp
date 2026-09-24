#include "app/SettingsView.h"
#include "app/Dialog.h"
#include "app/SettingsReport.h"
#include "app/Theme.h"
#include "common/Strings.h"
#include "config/Configuration.h"

namespace audioslave
{
namespace
{
// Read-only, scrollable list shown under a dialog's message.
std::unique_ptr<juce::Component> detailsBox (const juce::String& text)
{
    auto editor = std::make_unique<juce::TextEditor>();
    editor->setMultiLine (true, true);
    editor->setReadOnly (true);
    editor->setCaretVisible (false);
    editor->setScrollbarsShown (true);
    editor->setFont (theme::font (13.5f));
    editor->setColour (juce::TextEditor::backgroundColourId, theme::surface);
    editor->setColour (juce::TextEditor::textColourId, theme::textDim);
    editor->setIndents (10, 8);
    editor->setText (text, false);
    const int lines = juce::StringArray::fromLines (text).size();
    editor->setSize (100, juce::jlimit (60, 300, lines * 19 + 20));
    return editor;
}

int indexOf (const auto& list, auto value)
{
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i] == value)
            return static_cast<int> (i);
    return -1;
}
} // namespace

SettingsView::SettingsView (Request request, std::function<void()> onBack)
    : request_ (std::move (request)), onBack_ (std::move (onBack))
{
    title_.setText (utf8 ("Configurações"), juce::dontSendNotification);
    title_.setFont (theme::font (24.0f, true));
    subtitle_.setText (utf8 ("Aplicadas imediatamente pelo serviço — sem reinstalar nem reiniciar o computador."),
                       juce::dontSendNotification);
    subtitle_.setFont (theme::font (14.0f));
    subtitle_.setColour (juce::Label::textColourId, theme::textDim);
    section_.setText (utf8 ("ÁUDIO"), juce::dontSendNotification);
    section_.setFont (theme::font (12.0f, true));
    section_.setColour (juce::Label::textColourId, theme::textFaint);

    // Mandatory, as in the installer: shown locked.
    exclusive_.setButtonText (utf8 ("Remover modo exclusivo dos dispositivos de áudio"));
    exclusive_.setToggleState (true, juce::dontSendNotification);
    exclusive_.setEnabled (false);
    exclusiveNote_.setText (utf8 ("Obrigatório — é a funcionalidade principal do Audioslave."), juce::dontSendNotification);

    format_.setButtonText (utf8 ("Padronizar taxa de amostragem e profundidade de bits"));
    formatNote_.setText (utf8 ("Aplicado nos dispositivos que suportam o formato; o suporte é consultado no driver de cada "
                               "dispositivo."),
                         juce::dontSendNotification);
    rateLabel_.setText ("Taxa de amostragem", juce::dontSendNotification);
    bitsLabel_.setText ("Profundidade de bits", juce::dontSendNotification);
    int id = 1;
    for (auto r : supportedSampleRates)
        rate_.addItem (juce::String (r) + " Hz", id++);
    id = 1;
    for (auto b : supportedBitDepths)
        bits_.addItem (juce::String (b) + " bits", id++);

    disable_.setButtonText (utf8 ("Desabilitar dispositivos que não suportam a configuração selecionada"));
    disableNote_.setText (utf8 ("Desligado (padrão): os dispositivos incompatíveis são apenas ignorados e registrados no log. "
                                "Ligado: são desabilitados no Windows, sempre com confirmação antes."),
                          juce::dontSendNotification);

    for (auto* l : { &exclusiveNote_, &formatNote_, &disableNote_ })
    {
        l->setFont (theme::font (13.0f));
        l->setColour (juce::Label::textColourId, theme::textDim);
        l->setJustificationType (juce::Justification::topLeft);
    }
    for (auto* l : { &rateLabel_, &bitsLabel_ })
    {
        l->setFont (theme::font (12.0f, true));
        l->setColour (juce::Label::textColourId, theme::textFaint);
    }
    status_.setFont (theme::font (13.5f));
    status_.setColour (juce::Label::textColourId, theme::textDim);

    back_.setButtonText (utf8 ("← Voltar"));
    apply_.setButtonText ("Aplicar");
    cancel_.setButtonText (utf8 ("Descartar alterações"));
    theme::setVariant (back_, "ghost");
    theme::setVariant (apply_, "primary");
    theme::setVariant (cancel_, "ghost");

    auto changed = [this]
    {
        dirty_ = saved_.has_value() && chosen() != *saved_;
        updateEnabledState();
    };
    format_.onClick = changed;
    disable_.onClick = changed;
    rate_.onChange = changed;
    bits_.onChange = changed;
    back_.onClick = [this] { if (onBack_) onBack_(); };
    apply_.onClick = [this] { apply(); };
    cancel_.onClick = [this] { revert(); };

    for (auto* c : std::initializer_list<juce::Component*> { &title_, &subtitle_, &section_, &exclusive_, &exclusiveNote_,
                                                             &format_, &formatNote_, &rateLabel_, &rate_, &bitsLabel_, &bits_,
                                                             &disable_, &disableNote_, &status_, &back_, &apply_, &cancel_ })
        addAndMakeVisible (c);
    for (auto* b : std::initializer_list<juce::Button*> { &back_, &apply_, &cancel_, &format_, &disable_ })
        b->setMouseCursor (juce::MouseCursor::PointingHandCursor);

    setControls ({});
    updateEnabledState();
}

SettingsView::~SettingsView()
{
    *alive_ = false;
}

ipc::AudioSettings SettingsView::chosen() const
{
    ipc::AudioSettings s;
    s.formatStandardization = format_.getToggleState();
    s.sampleRate = supportedSampleRates[static_cast<size_t> (juce::jmax (0, rate_.getSelectedItemIndex()))];
    s.bitDepth = supportedBitDepths[static_cast<size_t> (juce::jmax (0, bits_.getSelectedItemIndex()))];
    s.disableIncompatibleDevices = disable_.getToggleState();
    return s;
}

void SettingsView::setControls (const ipc::AudioSettings& s)
{
    format_.setToggleState (s.formatStandardization, juce::dontSendNotification);
    disable_.setToggleState (s.disableIncompatibleDevices, juce::dontSendNotification);
    rate_.setSelectedItemIndex (juce::jmax (0, indexOf (supportedSampleRates, s.sampleRate)), juce::dontSendNotification);
    bits_.setSelectedItemIndex (juce::jmax (0, indexOf (supportedBitDepths, s.bitDepth)), juce::dontSendNotification);
}

void SettingsView::update (const ipc::StatusSnapshot* status)
{
    connected_ = status != nullptr;
    if (status != nullptr)
    {
        ipc::AudioSettings s;
        s.formatStandardization = status->formatStandardization;
        s.sampleRate = static_cast<std::uint32_t> (status->sampleRate);
        s.bitDepth = static_cast<std::uint16_t> (status->bitDepth);
        s.disableIncompatibleDevices = status->disableIncompatibleDevices;
        saved_ = s;
        savedConfirmed_ = status->disablePolicyConfirmed;
        if (! dirty_ && ! busy_)
            setControls (s);
        dirty_ = chosen() != s;
    }
    updateEnabledState();
}

void SettingsView::revert()
{
    dirty_ = false;
    if (saved_)
        setControls (*saved_);
    updateEnabledState();
}

void SettingsView::updateEnabledState()
{
    const bool editable = connected_ && ! busy_;
    const bool formatOn = format_.getToggleState();
    format_.setEnabled (editable);
    for (auto* c : std::initializer_list<juce::Component*> { &rate_, &bits_, &rateLabel_, &bitsLabel_, &disable_, &disableNote_ })
        c->setEnabled (editable && formatOn);
    apply_.setEnabled (editable && dirty_);
    cancel_.setEnabled (editable && dirty_);
    if (! busy_)
        status_.setText (! connected_ ? utf8 ("Sem conexão com o serviço Audioslave: as configurações não podem ser alteradas agora.")
                                      : dirty_ ? utf8 ("Alterações não aplicadas.")
                                               : juce::String(),
                         juce::dontSendNotification);
    repaint();
}

void SettingsView::setBusy (bool busy, const juce::String& text)
{
    busy_ = busy;
    updateEnabledState();
    if (busy)
        status_.setText (text, juce::dontSendNotification);
}

void SettingsView::apply()
{
    if (! request_ || busy_)
        return;
    const auto settings = chosen();
    setBusy (true, utf8 ("Consultando os dispositivos…"));
    auto alive = alive_;
    request_ (ipc::Command::analyze, ipc::toVar (settings), [this, alive, settings] (const ipc::Reply& reply)
    {
        if (! *alive)
            return;
        if (! reply.delivered || ! reply.ok)
        {
            setBusy (false);
            theme::showMessage (juce::MessageBoxIconType::WarningIcon, utf8 ("Não foi possível analisar os dispositivos"),
                                reply.error);
            return;
        }
        preview (settings, ipc::deviceReportsFromVar (reply.result.getProperty ("devices", {})));
    });
}

void SettingsView::preview (const ipc::AudioSettings& settings, const std::vector<DeviceReport>& devices)
{
    auto p = buildPreview (settings, devices);
    // Turning the policy on (or changing the format under it) is always
    // confirmed, even if no current device is affected: devices connected
    // later will be disabled without asking again.
    const bool sameTarget = saved_ && saved_->sampleRate == settings.sampleRate && saved_->bitDepth == settings.bitDepth
                            && saved_->formatStandardization;
    const bool policyIsNew = settings.formatStandardization && settings.disableIncompatibleDevices
                             && ! (saved_ && saved_->disableIncompatibleDevices && savedConfirmed_ && sameTarget);
    if (policyIsNew && ! p.disablesDevices)
    {
        p.needsConfirmation = true;
        p.title = utf8 ("Desabilitar dispositivos incompatíveis?");
        p.message = utf8 ("Nenhum dispositivo conectado agora será desabilitado. A partir de agora, dispositivos que não "
                          "suportarem ")
                    + juce::String (settings.sampleRate) + " Hz / " + juce::String (settings.bitDepth)
                    + utf8 (" bits serão desabilitados automaticamente, com aviso e registro no log.");
    }
    if (! p.needsConfirmation)
    {
        configure (settings);
        return;
    }
    theme::DialogOptions options;
    options.icon = p.disablesDevices ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::QuestionIcon;
    options.title = p.title;
    options.message = p.message;
    options.buttons = { "Continuar", "Cancelar" };
    options.destructive = p.disablesDevices;
    if (p.details.isNotEmpty())
        options.extra = detailsBox (p.details);
    auto alive = alive_;
    theme::showDialog (std::move (options), [this, alive, settings] (int button, juce::Component*)
    {
        if (! *alive)
            return;
        if (button != 0)
        {
            // Cancelar: nothing was changed; the choices stay for editing.
            setBusy (false);
            return;
        }
        configure (settings);
    });
}

void SettingsView::configure (ipc::AudioSettings settings)
{
    // With the policy on, reaching this point is the confirmation: whatever
    // it does now or later was shown above (or was confirmed before for this
    // exact format).
    if (settings.disableIncompatibleDevices)
        settings.confirmDisable = true;
    setBusy (true, utf8 ("Aplicando…"));
    auto alive = alive_;
    request_ (ipc::Command::configure, ipc::toVar (settings), [this, alive, settings] (const ipc::Reply& reply)
    {
        if (! *alive)
            return;
        setBusy (false);
        if (! reply.delivered || ! reply.ok)
        {
            theme::showMessage (juce::MessageBoxIconType::WarningIcon, utf8 ("Não foi possível aplicar a configuração"),
                                reply.error);
            return;
        }
        dirty_ = false;
        if (reply.status)
            update (&*reply.status);
        const auto outcome = buildOutcome (settings, ipc::deviceReportsFromVar (reply.result.getProperty ("devices", {})),
                                           reply.result.getProperty ("paused", false));
        theme::DialogOptions options;
        options.icon = outcome.problems ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::InfoIcon;
        options.title = outcome.title;
        options.message = outcome.message;
        if (outcome.details.isNotEmpty())
            options.extra = detailsBox (outcome.details);
        theme::showDialog (std::move (options));
    });
}

void SettingsView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    theme::paintCard (g, card_.toFloat());
}

void SettingsView::resized()
{
    auto area = getLocalBounds().reduced (28, 22);
    auto head = area.removeFromTop (64);
    back_.setBounds (head.removeFromRight (120).withSizeKeepingCentre (120, 36));
    title_.setBounds (head.removeFromTop (36));
    subtitle_.setBounds (head);
    area.removeFromTop (18);

    auto footer = area.removeFromBottom (40);
    apply_.setBounds (footer.removeFromLeft (150));
    footer.removeFromLeft (10);
    cancel_.setBounds (footer.removeFromLeft (200));
    footer.removeFromLeft (14);
    status_.setBounds (footer);
    area.removeFromBottom (18);

    card_ = area.withHeight (juce::jmin (area.getHeight(), 372));
    auto card = card_.reduced (22, 18);
    const auto indent = [] (juce::Rectangle<int> r) { return r.withTrimmedLeft (48); };
    section_.setBounds (card.removeFromTop (20));
    card.removeFromTop (8);
    exclusive_.setBounds (card.removeFromTop (28));
    exclusiveNote_.setBounds (indent (card.removeFromTop (22)));
    card.removeFromTop (12);
    format_.setBounds (card.removeFromTop (28));
    formatNote_.setBounds (indent (card.removeFromTop (22)));
    card.removeFromTop (8);
    auto labels = indent (card.removeFromTop (20));
    auto row = indent (card.removeFromTop (38));
    rateLabel_.setBounds (labels.removeFromLeft (220));
    rate_.setBounds (row.removeFromLeft (200).reduced (0, 2));
    labels.removeFromLeft (0);
    row.removeFromLeft (20);
    bitsLabel_.setBounds (labels.removeFromLeft (200));
    bits_.setBounds (row.removeFromLeft (160).reduced (0, 2));
    card.removeFromTop (16);
    disable_.setBounds (card.removeFromTop (28));
    disableNote_.setBounds (indent (card.removeFromTop (40)));
}
} // namespace audioslave
