#include "app/StatusWindow.h"
#include "AudioslaveVersion.h"
#include "app/TrayIcon.h"
#include "common/Strings.h"

namespace audioslave
{
namespace
{
juce::String flowText (EndpointFlow flow)
{
    return flow == EndpointFlow::capture ? utf8 ("Captura") : utf8 ("Reprodução");
}

juce::String stateText (EndpointState state)
{
    switch (state)
    {
        case EndpointState::active:     return "Ativo";
        case EndpointState::unplugged:  return "Desconectado";
        case EndpointState::disabled:   return "Desativado";
        case EndpointState::notPresent: break;
    }
    return "Ausente";
}

juce::String exclusiveText (const juce::String& value)
{
    if (value == "blocked")
        return "Bloqueado";
    if (value == "allowed")
        return "PERMITIDO";
    return "Desconhecido";
}

juce::Colour stateColour (TrayController::UiState s)
{
    switch (s)
    {
        case TrayController::UiState::running:  return theme::ok;
        case TrayController::UiState::paused:   return theme::warning;
        case TrayController::UiState::starting:
        case TrayController::UiState::stopping: return theme::accentBright;
        default:                                return theme::danger;
    }
}
} // namespace

class StatusWindow::Content final : public juce::Component, private juce::TableListBoxModel
{
public:
    explicit Content (Actions actions) : actions_ (std::move (actions))
    {
        logo_ = TrayIcon::logoImage (false, 64);

        title_.setText (juce::String ("Audioslave ") + AUDIOSLAVE_VERSION_STRING, juce::dontSendNotification);
        title_.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        subtitle_.setText (utf8 ("Proteção contra o modo exclusivo dos dispositivos de áudio"), juce::dontSendNotification);
        subtitle_.setColour (juce::Label::textColourId, theme::textDim);
        state_.setFont (juce::FontOptions (16.0f, juce::Font::bold));

        for (auto* l : { &title_, &subtitle_, &state_, &exclusive_, &format_, &mode_, &scan_, &totals_, &logs_ })
            addAndMakeVisible (*l);
        for (auto* l : { &exclusive_, &format_, &mode_, &scan_, &totals_, &logs_ })
            l->setFont (juce::FontOptions (14.0f));
        logs_.setColour (juce::Label::textColourId, theme::textDim);

        auto& header = table_.getHeader();
        header.addColumn (utf8 ("Dispositivo"), 1, 290, 120, -1, juce::TableHeaderComponent::defaultFlags);
        header.addColumn ("Tipo", 2, 90);
        header.addColumn ("Estado", 3, 100);
        header.addColumn ("Modo exclusivo", 4, 120);
        header.addColumn ("Formato", 5, 140);
        header.setStretchToFitActive (true);
        table_.setModel (this);
        table_.setRowHeight (24);
        table_.setColour (juce::ListBox::backgroundColourId, theme::surface);
        addAndMakeVisible (table_);

        toggle_.onClick = [this]
        {
            if (paused_ ? static_cast<bool> (actions_.resume) : static_cast<bool> (actions_.pause))
                paused_ ? actions_.resume() : actions_.pause();
        };
        scanButton_.onClick = [this] { if (actions_.scan) actions_.scan(); };
        logsButton_.onClick = [this] { if (actions_.openLogs) actions_.openLogs(); };
        for (auto* b : { &toggle_, &scanButton_, &logsButton_ })
            addAndMakeVisible (*b);

        setSize (820, 560);
    }

    ~Content() override { table_.setModel (nullptr); }

    void update (const TrayController& c)
    {
        const auto ui = c.uiState();
        state_.setText ("Status: " + TrayController::uiStateText (ui), juce::dontSendNotification);
        state_.setColour (juce::Label::textColourId, stateColour (ui));

        const auto menu = c.menu();
        paused_ = ! menu.pauseEnabled && menu.resumeEnabled;
        toggle_.setButtonText (paused_ ? "Retomar monitoramento" : "Pausar monitoramento");
        toggle_.setEnabled (menu.pauseEnabled || menu.resumeEnabled);
        scanButton_.setEnabled (menu.scanEnabled);

        rows_.clear();
        if (const auto& s = c.status())
        {
            exclusive_.setText (s->exclusiveProtection ? utf8 ("Proteção contra modo exclusivo: ATIVADA")
                                                       : utf8 ("Proteção contra modo exclusivo: DESATIVADA (config.ini)"),
                                juce::dontSendNotification);
            format_.setText (s->formatStandardization ? utf8 ("Padronização de formato: ATIVADA (") + s->formatTarget + ")"
                                                      : utf8 ("Padronização de formato: DESATIVADA"),
                             juce::dontSendNotification);
            mode_.setText (s->mode == "service" ? utf8 ("Modo: serviço do Windows (Audioslave)")
                                                : utf8 ("Modo: portátil (sem serviço instalado)"),
                           juce::dontSendNotification);
            if (s->hasScanned)
            {
                const auto& r = s->lastScan;
                scan_.setText (utf8 ("Última verificação: ") + s->lastScanTime.formatted ("%d/%m/%Y %H:%M:%S") + " - "
                                   + juce::String (r.endpointsScanned) + " dispositivo(s), " + juce::String (r.exclusiveFixed)
                                   + utf8 (" correção(ões), ") + juce::String (r.errors()) + " erro(s)",
                               juce::dontSendNotification);
            }
            else
            {
                scan_.setText (utf8 ("Última verificação: ainda não executada"), juce::dontSendNotification);
            }
            totals_.setText (utf8 ("Desde o início: ") + juce::String (s->totalExclusiveFixes)
                                 + utf8 (" correção(ões) de modo exclusivo, ") + juce::String (s->totalFormatChanges)
                                 + utf8 (" mudança(s) de formato"),
                             juce::dontSendNotification);
            logs_.setText ("Logs: " + s->logsDir, juce::dontSendNotification);
            rows_ = s->endpoints;
        }
        else
        {
            exclusive_.setText (utf8 ("Sem conexão com o serviço Audioslave."), juce::dontSendNotification);
            for (auto* l : { &format_, &mode_, &scan_, &totals_ })
                l->setText ({}, juce::dontSendNotification);
        }
        table_.updateContent();
        table_.repaint();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::background);
        g.drawImage (logo_, juce::Rectangle<float> (20.0f, 18.0f, 56.0f, 56.0f), juce::RectanglePlacement::centred);
        g.setColour (theme::outline);
        g.fillRect (20, 88, getWidth() - 40, 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (20);
        auto head = area.removeFromTop (60);
        head.removeFromLeft (70);
        title_.setBounds (head.removeFromTop (30));
        subtitle_.setBounds (head);
        area.removeFromTop (16);

        state_.setBounds (area.removeFromTop (26));
        for (auto* l : { &exclusive_, &format_, &mode_, &scan_, &totals_, &logs_ })
            l->setBounds (area.removeFromTop (22));
        area.removeFromTop (8);

        auto buttons = area.removeFromBottom (32);
        toggle_.setBounds (buttons.removeFromLeft (190));
        buttons.removeFromLeft (8);
        scanButton_.setBounds (buttons.removeFromLeft (140));
        buttons.removeFromLeft (8);
        logsButton_.setBounds (buttons.removeFromLeft (170));
        area.removeFromBottom (12);
        table_.setBounds (area);
    }

private:
    int getNumRows() override { return static_cast<int> (rows_.size()); }

    void paintRowBackground (juce::Graphics& g, int row, int, int, bool selected) override
    {
        g.fillAll (selected ? theme::accent.withAlpha (0.35f) : (row % 2 == 0 ? theme::surface : theme::surfaceAlt));
    }

    void paintCell (juce::Graphics& g, int row, int column, int width, int height, bool) override
    {
        if (row < 0 || row >= static_cast<int> (rows_.size()))
            return;
        const auto& e = rows_[static_cast<size_t> (row)];
        juce::String text;
        auto colour = theme::text;
        switch (column)
        {
            case 1: text = e.name + (e.isDefault ? utf8 ("  (padrão)") : juce::String()); break;
            case 2: text = flowText (e.flow); break;
            case 3:
                text = stateText (e.state);
                colour = e.state == EndpointState::active ? theme::text : theme::textDim;
                break;
            case 4:
                text = exclusiveText (e.exclusive);
                colour = e.exclusive == "blocked" ? theme::ok : e.exclusive == "allowed" ? theme::danger : theme::warning;
                break;
            case 5: text = e.format; break;
            default: break;
        }
        g.setColour (colour);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (text, 6, 0, width - 8, height, juce::Justification::centredLeft, true);
    }

    Actions actions_;
    juce::Image logo_;
    juce::Label title_, subtitle_, state_, exclusive_, format_, mode_, scan_, totals_, logs_;
    juce::TableListBox table_;
    juce::TextButton toggle_ { "Pausar monitoramento" }, scanButton_ { "Verificar agora" },
        logsButton_ { "Abrir pasta de logs" };
    std::vector<EndpointStatus> rows_;
    bool paused_ = false;
};

StatusWindow::StatusWindow (Actions actions, std::function<void()> onClose)
    : juce::DocumentWindow ("Audioslave", theme::background, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      onClose_ (std::move (onClose))
{
    setLookAndFeel (&lookAndFeel_);
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (std::move (actions)), true);
    setResizable (true, false);
    setResizeLimits (640, 460, 1600, 1200);
    setIcon (TrayIcon::logoImage (false, 32));
    centreWithSize (getWidth(), getHeight());
}

StatusWindow::~StatusWindow()
{
    clearContentComponent();
    setLookAndFeel (nullptr);
}

void StatusWindow::update (const TrayController& controller)
{
    if (auto* content = dynamic_cast<Content*> (getContentComponent()))
        content->update (controller);
}

void StatusWindow::closeButtonPressed()
{
    if (onClose_)
        onClose_();
}
} // namespace audioslave
