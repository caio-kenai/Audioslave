#include "platform/windows/WinCommon.h"
#include "installer/SetupWizard.h"
#include "app/Dialog.h"
#include "AudioslaveAssets.h"
#include "AudioslaveVersion.h"
#include "app/SettingsReport.h"
#include "audio/windows/WindowsAudioEndpointEnumerator.h"
#include "audio/windows/WindowsAudioFormatPolicy.h"
#include "audio/windows/WindowsExclusiveModePolicy.h"
#include "common/Branding.h"
#include "common/Strings.h"
#include "config/Configuration.h"
#include "installer/InstallerCore.h"
#include "core/WatchdogEngine.h"
#include "platform/windows/Paths.h"
#include "platform/windows/ScopedComInit.h"

namespace audioslave::setup
{
struct SetupWizard::Worker final : public juce::Thread
{
    Worker (Options o, SetupWizard& owner)
        : juce::Thread ("Audioslave installer"), options (std::move (o)), alive (owner.alive_), wizard (&owner)
    {
    }

    ~Worker() override { waitForThreadToExit (-1); }

    void run() override
    {
        juce::String error;
        auto aliveFlag = alive;
        auto* target = wizard;
        const bool ok = install (options, [aliveFlag, target] (int percent, const juce::String& text)
        {
            juce::MessageManager::callAsync ([aliveFlag, target, percent, text]
            {
                if (! aliveFlag->load())
                    return;
                target->progressValue_ = percent / 100.0;
                target->status_.setText (text, juce::dontSendNotification);
            });
        }, error);
        juce::MessageManager::callAsync ([aliveFlag, target, ok, error]
        {
            if (aliveFlag->load())
                target->finished (ok, error);
        });
    }

    Options options;
    std::shared_ptr<std::atomic<bool>> alive;
    SetupWizard* wizard;
};

SetupWizard::SetupWizard (Options options, std::function<void (int)> onFinished)
    : options_ (std::move (options)), onFinished_ (std::move (onFinished))
{
    logo_ = juce::ImageCache::getFromMemory (AudioslaveAssets::normal256_png, AudioslaveAssets::normal256_pngSize);
    const bool upgrade = installedLocation().isNotEmpty();

    title_.setText (brand::productName, juce::dontSendNotification);
    title_.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    subtitle_.setText (utf8 ("Versão ") + AUDIOSLAVE_VERSION_STRING
                           + (upgrade ? utf8 (" — atualização da instalação existente") : utf8 (" — instalação")),
                       juce::dontSendNotification);
    subtitle_.setColour (juce::Label::textColourId, theme::textDim);

    featuresHeader_.setText ("Funcionalidades", juce::dontSendNotification);
    featuresHeader_.setFont (juce::FontOptions (15.0f, juce::Font::bold));

    // Feature 1 is mandatory: checked and locked.
    exclusive_.setButtonText (utf8 ("Remover modo exclusivo dos dispositivos de áudio"));
    exclusive_.setToggleState (true, juce::dontSendNotification);
    exclusive_.setEnabled (false);
    exclusiveNote_.setText (utf8 ("Obrigatório — é a funcionalidade principal do Audioslave."), juce::dontSendNotification);
    exclusiveNote_.setColour (juce::Label::textColourId, theme::textDim);

    // Feature 2 defaults to the existing configuration (upgrade) or off.
    const auto cfg = loadConfiguration (paths::configFile(), false).config;
    format_.setButtonText (utf8 ("Padronizar taxa de amostragem e profundidade de bits dos dispositivos de áudio"));
    format_.setToggleState (cfg.formatStandardization, juce::dontSendNotification);
    format_.onClick = [this] { updateFormatControls(); };
    rateLabel_.setText ("Taxa de amostragem:", juce::dontSendNotification);
    bitsLabel_.setText ("Profundidade de bits:", juce::dontSendNotification);
    int id = 1;
    for (auto r : supportedSampleRates)
        rate_.addItem (juce::String (r) + " Hz", id++);
    id = 1;
    for (auto b : supportedBitDepths)
        bits_.addItem (juce::String (b) + "-bit", id++);
    for (int i = 0; i < static_cast<int> (supportedSampleRates.size()); ++i)
        if (supportedSampleRates[static_cast<size_t> (i)] == cfg.sampleRate)
            rate_.setSelectedItemIndex (i, juce::dontSendNotification);
    for (int i = 0; i < static_cast<int> (supportedBitDepths.size()); ++i)
        if (supportedBitDepths[static_cast<size_t> (i)] == cfg.bitDepth)
            bits_.setSelectedItemIndex (i, juce::dontSendNotification);
    formatNote_.setText (utf8 ("Aplicado apenas nos dispositivos que suportam o formato escolhido; os demais não são alterados."),
                         juce::dontSendNotification);
    formatNote_.setColour (juce::Label::textColourId, theme::textDim);

    // New option, off by default (an upgrade keeps the current choice).
    disable_.setButtonText (utf8 ("Desabilitar dispositivos que não suportam a configuração selecionada"));
    disable_.setToggleState (options_.disableSet ? options_.disableIncompatible : cfg.disableIncompatibleDevices,
                             juce::dontSendNotification);
    disableNote_.setText (utf8 ("Opcional. Os dispositivos afetados são listados para confirmação antes da instalação; "
                                "pode ser alterado depois em Configurações."),
                          juce::dontSendNotification);
    disableNote_.setColour (juce::Label::textColourId, theme::textDim);

    dirLabel_.setText (utf8 ("Pasta de instalação:"), juce::dontSendNotification);
    dir_.setText (options_.dir, false);
    browse_.onClick = [this] { browse(); };

    launch_.setButtonText ("Iniciar o Audioslave ao concluir");
    launch_.setToggleState (options_.launchTray, juce::dontSendNotification);


    progress_.setPercentageDisplay (false);
    progress_.setVisible (false);

    theme::setVariant (install_, "primary");
    theme::setVariant (cancel_, "ghost");
    theme::setVariant (browse_, "secondary");
    title_.setFont (theme::font (28.0f, true));
    subtitle_.setFont (theme::font (14.5f));
    featuresHeader_.setFont (theme::font (12.0f, true));
    featuresHeader_.setColour (juce::Label::textColourId, theme::textFaint);
    featuresHeader_.setText ("FUNCIONALIDADES", juce::dontSendNotification);
    dirLabel_.setFont (theme::font (12.0f, true));
    dirLabel_.setColour (juce::Label::textColourId, theme::textFaint);
    dirLabel_.setText (utf8 ("PASTA DE INSTALAÇÃO"), juce::dontSendNotification);
    for (auto* l : { &exclusiveNote_, &formatNote_, &disableNote_ })
        l->setFont (theme::font (13.0f));
    for (auto* l : { &rateLabel_, &bitsLabel_, &status_ })
        l->setFont (theme::font (14.0f));
    dir_.setFont (theme::font (14.5f));
    dir_.setIndents (10, 7);

    install_.onClick = [this]
    {
        if (done_)
            onFinished_ (succeeded_ ? 0 : 1);
        else if (! running_)
            confirmThenInstall();
    };
    cancel_.onClick = [this] { requestClose(); };

    for (auto* c : std::initializer_list<juce::Component*> { &title_, &subtitle_, &featuresHeader_, &exclusive_, &exclusiveNote_,
                                                             &format_, &rateLabel_, &rate_, &bitsLabel_, &bits_, &formatNote_,
                                                             &disable_, &disableNote_,
                                                             &dirLabel_, &dir_, &browse_, &launch_, &progress_,
                                                             &status_, &install_, &cancel_ })
        addChildComponent (c);
    for (auto* c : getChildren())
        if (c != &progress_)
            c->setVisible (true);

    updateFormatControls();
    setSize (760, 650);
}

SetupWizard::~SetupWizard()
{
    alive_->store (false);
    worker_.reset();
}

void SetupWizard::updateFormatControls()
{
    const bool on = format_.getToggleState() && ! running_ && ! done_;
    for (auto* c : std::initializer_list<juce::Component*> { &rateLabel_, &rate_, &bitsLabel_, &bits_, &disable_, &disableNote_ })
        c->setEnabled (on);
}

void SetupWizard::confirmThenInstall()
{
    if (! format_.getToggleState())
    {
        startInstall();
        return;
    }

    // Preview on this machine, exactly as the service will judge it, with the
    // same alerts as the Configurações screen (bit-depth limits, devices
    // without the sample rate, devices that will be disabled).
    const bool disable = disable_.getToggleState();
    ipc::AudioSettings settings;
    settings.formatStandardization = true;
    settings.sampleRate = supportedSampleRates[static_cast<size_t> (juce::jmax (0, rate_.getSelectedItemIndex()))];
    settings.bitDepth = supportedBitDepths[static_cast<size_t> (juce::jmax (0, bits_.getSelectedItemIndex()))];
    settings.disableIncompatibleDevices = disable;

    std::vector<DeviceReport> devices;
    {
        const win::ScopedComInit com (COINIT_APARTMENTTHREADED);
        win::WindowsAudioEndpointEnumerator enumerator;
        win::WindowsExclusiveModePolicy exclusive;
        win::WindowsAudioFormatPolicy format;
        DeviceStateStore state (paths::programDataDir().getChildFile ("devices.json"));
        WatchdogEngine::Options engineOptions;
        engineOptions.deviceState = &state;
        auto candidate = loadConfiguration (paths::configFile(), false).config;
        candidate.formatStandardization = true;
        candidate.sampleRate = settings.sampleRate;
        candidate.bitDepth = settings.bitDepth;
        candidate.disableIncompatibleDevices = disable;
        WatchdogEngine engine (enumerator, exclusive, format, candidate, engineOptions);
        devices = engine.analyze (candidate);
    }

    auto preview = buildPreview (settings, devices);
    // Turning the policy on is always confirmed, even with nothing to disable now.
    if (disable && ! preview.disablesDevices)
    {
        preview.needsConfirmation = true;
        preview.title = utf8 ("Desabilitar dispositivos incompatíveis?");
        preview.message = utf8 ("Nenhum dispositivo conectado agora será desabilitado. Dispositivos conectados depois que "
                                "não suportarem ")
                          + juce::String (settings.sampleRate) + " Hz / " + juce::String (settings.bitDepth)
                          + utf8 (" bits serão desabilitados automaticamente, com aviso e registro no log.");
    }
    if (! preview.needsConfirmation)
    {
        startInstall();
        return;
    }
    theme::DialogOptions options;
    options.icon = disable ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::QuestionIcon;
    options.title = preview.title;
    options.message = preview.message;
    options.buttons = { "Continuar", "Cancelar" };
    options.destructive = disable;
    if (preview.details.isNotEmpty())
    {
        auto details = std::make_unique<juce::TextEditor>();
        details->setMultiLine (true, true);
        details->setReadOnly (true);
        details->setCaretVisible (false);
        details->setFont (theme::font (13.5f));
        details->setColour (juce::TextEditor::backgroundColourId, theme::surface);
        details->setColour (juce::TextEditor::textColourId, theme::textDim);
        details->setIndents (10, 8);
        details->setText (preview.details, false);
        details->setSize (100, juce::jlimit (60, 260, juce::StringArray::fromLines (preview.details).size() * 19 + 20));
        options.extra = std::move (details);
    }
    auto alive = alive_;
    theme::showDialog (std::move (options), [this, alive] (int button, juce::Component*)
    {
        if (alive->load() && button == 0)
            startInstall();
    });
}

void SetupWizard::browse()
{
    chooser_ = std::make_unique<juce::FileChooser> (utf8 ("Escolha a pasta de instalação"), juce::File (dir_.getText()));
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                           [this] (const juce::FileChooser& fc)
                           {
                               auto chosen = fc.getResult();
                               if (chosen == juce::File())
                                   return;
                               // Always install into a dedicated folder.
                               if (chosen.getFileName() != brand::productName)
                                   chosen = chosen.getChildFile (brand::productName);
                               dir_.setText (chosen.getFullPathName(), false);
                           });
}

void SetupWizard::startInstall()
{
    auto dir = dir_.getText().trim();
    while (dir.endsWithChar ('\\'))
        dir = dir.dropLastCharacters (1);
    if (dir.length() < 4 || dir[1] != ':')
    {
        theme::showMessage (juce::MessageBoxIconType::WarningIcon, utf8 ("Pasta inválida"),
                            utf8 ("Informe uma pasta de instalação válida, por exemplo C:\\Program Files\\Audioslave."));
        return;
    }

    options_.dir = dir;
    options_.formatSet = true;
    options_.format = format_.getToggleState();
    options_.sampleRate = supportedSampleRates[static_cast<size_t> (juce::jmax (0, rate_.getSelectedItemIndex()))];
    options_.bitDepth = supportedBitDepths[static_cast<size_t> (juce::jmax (0, bits_.getSelectedItemIndex()))];
    options_.disableSet = true;
    options_.disableIncompatible = format_.getToggleState() && disable_.getToggleState();
    options_.launchTray = launch_.getToggleState();

    running_ = true;
    for (auto* c : std::initializer_list<juce::Component*> { &format_, &disable_, &dir_, &browse_, &launch_, &install_, &cancel_ })
        c->setEnabled (false);
    updateFormatControls();
    progress_.setVisible (true);

    worker_ = std::make_unique<Worker> (options_, *this);
    worker_->startThread();
}

void SetupWizard::finished (bool ok, const juce::String& error)
{
    running_ = false;
    done_ = true;
    succeeded_ = ok;
    install_.setButtonText ("Concluir");
    install_.setEnabled (true);
    progressValue_ = ok ? 1.0 : progressValue_;

    if (ok)
    {
        status_.setText (utf8 ("Instalação concluída."), juce::dontSendNotification);
        theme::showMessage (juce::MessageBoxIconType::InfoIcon, utf8 ("Instalação concluída"),
                            utf8 ("O Audioslave ") + AUDIOSLAVE_VERSION_STRING
                                + utf8 (" foi instalado e o serviço está em execução.\n\nLogs: ") + options_.dir + "\\logs");
    }
    else
    {
        status_.setText (utf8 ("A instalação falhou."), juce::dontSendNotification);
        status_.setColour (juce::Label::textColourId, theme::danger);
        theme::showMessage (juce::MessageBoxIconType::WarningIcon, utf8 ("A instalação falhou"), error);
    }
}

void SetupWizard::requestClose()
{
    if (running_)
        return; // never leave a half-finished installation behind
    onFinished_ (done_ ? (succeeded_ ? 0 : 1) : 1);
}

void SetupWizard::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    if (logo_.isValid())
    {
        // drawImage uses the current fill's opacity.
        g.setOpacity (1.0f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logo_, juce::Rectangle<float> (28.0f, 24.0f, 64.0f, 64.0f), juce::RectanglePlacement::centred);
    }
    theme::paintCard (g, featuresCard_.toFloat());
    theme::paintCard (g, locationCard_.toFloat());
}

void SetupWizard::resized()
{
    auto area = getLocalBounds().reduced (28, 24);
    auto head = area.removeFromTop (64);
    head.removeFromLeft (80);
    title_.setBounds (head.removeFromTop (38));
    subtitle_.setBounds (head);
    area.removeFromTop (22);

    auto indent = [] (juce::Rectangle<int> r, int by) { return r.withTrimmedLeft (by); };

    featuresCard_ = area.removeFromTop (280);
    auto card = featuresCard_.reduced (20, 16);
    featuresHeader_.setBounds (card.removeFromTop (20));
    card.removeFromTop (6);
    exclusive_.setBounds (card.removeFromTop (28));
    exclusiveNote_.setBounds (indent (card.removeFromTop (20), 48));
    card.removeFromTop (10);
    format_.setBounds (card.removeFromTop (28));
    auto row = indent (card.removeFromTop (38), 48);
    rateLabel_.setBounds (row.removeFromLeft (150));
    rate_.setBounds (row.removeFromLeft (150).reduced (0, 3));
    row.removeFromLeft (24);
    bitsLabel_.setBounds (row.removeFromLeft (150));
    bits_.setBounds (row.removeFromLeft (120).reduced (0, 3));
    formatNote_.setBounds (indent (card.removeFromTop (24), 48));
    card.removeFromTop (8);
    disable_.setBounds (indent (card.removeFromTop (28), 48));
    disableNote_.setBounds (indent (card.removeFromTop (36), 96));
    area.removeFromTop (16);

    locationCard_ = area.removeFromTop (132);
    card = locationCard_.reduced (20, 16);
    dirLabel_.setBounds (card.removeFromTop (20));
    card.removeFromTop (6);
    row = card.removeFromTop (36);
    browse_.setBounds (row.removeFromRight (120));
    row.removeFromRight (10);
    dir_.setBounds (row);
    card.removeFromTop (10);
    launch_.setBounds (card.removeFromTop (28));

    auto bottom = area.removeFromBottom (40);
    cancel_.setBounds (bottom.removeFromRight (120));
    bottom.removeFromRight (10);
    install_.setBounds (bottom.removeFromRight (140));
    status_.setBounds (bottom.reduced (0, 4));
    area.removeFromBottom (14);
    progress_.setBounds (area.removeFromBottom (8));
}
} // namespace audioslave::setup
