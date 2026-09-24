#include "app/StatusWindow.h"
#include "AudioslaveVersion.h"
#include "app/Theme.h"
#include "app/TrayIcon.h"
#include "app/TrayMenu.h"
#include "common/Strings.h"

namespace audioslave
{
namespace
{
const char* const windowStateKey = "statusWindow";

// Per-user UI preferences (the configuration in ProgramData is machine-wide
// and read-only for users).
std::unique_ptr<juce::PropertiesFile> openUiSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "ui";
    o.filenameSuffix = ".settings";
    o.folderName = "Audioslave";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = -1;
    return std::make_unique<juce::PropertiesFile> (o);
}

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

// A summary card: small caption, large value, one line of detail.
struct Card
{
    juce::String caption, value, detail;
    juce::Colour valueColour = theme::text;

    void paint (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        theme::paintCard (g, area);
        auto r = area.reduced (18.0f, 14.0f);
        g.setColour (theme::textFaint);
        g.setFont (theme::font (12.0f, true));
        g.drawText (caption, r.removeFromTop (18.0f), juce::Justification::centredLeft, true);
        r.removeFromTop (4.0f);
        g.setColour (valueColour);
        g.setFont (theme::font (21.0f, true));
        g.drawFittedText (value, r.removeFromTop (30.0f).toNearestInt(), juce::Justification::centredLeft, 1);
        g.setColour (theme::textDim);
        g.setFont (theme::font (13.0f));
        g.drawFittedText (detail, r.toNearestInt(), juce::Justification::topLeft, 2);
    }
};
} // namespace

class StatusWindow::Content final : public juce::Component, private juce::TableListBoxModel
{
public:
    explicit Content (Actions actions) : actions_ (std::move (actions))
    {
        logo_ = TrayIcon::logoImage (false, 256);

        auto& header = table_.getHeader();
        header.addColumn (utf8 ("Dispositivo"), 1, 300, 140, -1, juce::TableHeaderComponent::defaultFlags);
        header.addColumn ("Tipo", 2, 110);
        header.addColumn ("Estado", 3, 120);
        header.addColumn ("Modo exclusivo", 4, 140);
        header.addColumn ("Formato", 5, 150);
        header.setStretchToFitActive (true);
        header.setPopupMenuActive (false);
        table_.setModel (this);
        table_.setRowHeight (34);
        table_.setHeaderHeight (34);
        table_.getViewport()->setScrollBarThickness (10);
        table_.getViewport()->setScrollBarsShown (true, false);
        addAndMakeVisible (table_);

        theme::setVariant (toggle_, "primary");
        theme::setVariant (scan_, "secondary");
        theme::setVariant (logs_, "ghost");
        toggle_.onClick = [this]
        {
            auto& action = paused_ ? actions_.resume : actions_.pause;
            if (action)
                action();
        };
        scan_.onClick = [this] { if (actions_.scan) actions_.scan(); };
        logs_.onClick = [this] { if (actions_.openLogs) actions_.openLogs(); };
        for (auto* b : { &toggle_, &scan_, &logs_ })
        {
            b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
            addAndMakeVisible (*b);
        }

        setSize (940, 660);
    }

    ~Content() override { table_.setModel (nullptr); }

    void update (const TrayController& c)
    {
        const auto ui = c.uiState();
        stateText_ = TrayController::uiStateText (ui);
        stateColour_ = trayStateColour (ui);

        const auto menu = c.menu();
        paused_ = ! menu.pauseEnabled && menu.resumeEnabled;
        toggle_.setButtonText (paused_ ? "Retomar monitoramento" : "Pausar monitoramento");
        toggle_.setEnabled (menu.pauseEnabled || menu.resumeEnabled);
        scan_.setEnabled (menu.scanEnabled);

        rows_.clear();
        if (const auto& s = c.status())
        {
            exclusive_ = { "MODO EXCLUSIVO", s->exclusiveProtection ? "Bloqueado" : "Desativado",
                           s->exclusiveProtection ? utf8 ("Nenhum aplicativo assume o controle exclusivo dos dispositivos.")
                                                  : utf8 ("Proteção desligada no config.ini."),
                           s->exclusiveProtection ? theme::ok : theme::warning };
            format_ = { utf8 ("FORMATO PADRÃO"), s->formatStandardization ? s->formatTarget : juce::String ("Livre"),
                        s->formatStandardization ? utf8 ("Aplicado nos dispositivos que suportam o formato.")
                                                 : utf8 ("Padronização de formato desativada."),
                        theme::text };
            activity_ = { "ATIVIDADE", juce::String (s->totalExclusiveFixes) + utf8 (" correção(ões)"),
                          s->hasScanned ? utf8 ("Última verificação às ") + s->lastScanTime.formatted ("%H:%M:%S") + ", "
                                              + juce::String (s->lastScan.endpointsScanned) + " dispositivo(s)"
                                        : utf8 ("Aguardando a primeira verificação."),
                          theme::text };
            modeText_ = s->mode == "service" ? utf8 ("Serviço do Windows") : utf8 ("Modo portátil");
            logsDir_ = s->logsDir;
            rows_ = s->endpoints;
        }
        else
        {
            exclusive_ = { "MODO EXCLUSIVO", "--", utf8 ("Sem conexão com o serviço Audioslave."), theme::textDim };
            format_ = { utf8 ("FORMATO PADRÃO"), "--", {}, theme::textDim };
            activity_ = { "ATIVIDADE", "--", {}, theme::textDim };
            modeText_ = {};
        }
        table_.updateContent();
        table_.repaint();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::background);

        // Header: logo, name, version, status pill. drawImage uses the
        // current fill's opacity, so make sure it is opaque.
        g.setOpacity (1.0f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logo_, logoArea_.toFloat(), juce::RectanglePlacement::centred);
        g.setColour (theme::text);
        g.setFont (theme::font (26.0f, true));
        g.drawText ("Audioslave", titleArea_, juce::Justification::bottomLeft, false);
        g.setColour (theme::textDim);
        g.setFont (theme::font (14.0f));
        g.drawText (utf8 ("Versão ") + AUDIOSLAVE_VERSION_STRING + (modeText_.isNotEmpty() ? utf8 ("  ·  ") + modeText_ : juce::String()),
                    subtitleArea_, juce::Justification::topLeft, false);
        theme::paintPill (g, pillArea_.toFloat(), stateColour_, stateText_);

        exclusive_.paint (g, cards_[0].toFloat());
        format_.paint (g, cards_[1].toFloat());
        activity_.paint (g, cards_[2].toFloat());

        // Devices card.
        theme::paintCard (g, devicesCard_.toFloat());
        g.setColour (theme::text);
        g.setFont (theme::font (16.0f, true));
        g.drawText ("Dispositivos monitorados", devicesTitle_, juce::Justification::centredLeft, false);
        g.setColour (theme::textFaint);
        g.setFont (theme::font (13.0f));
        g.drawText (juce::String (static_cast<int> (rows_.size())) + " endpoint(s)", devicesTitle_, juce::Justification::centredRight,
                    false);

        if (logsDir_.isNotEmpty())
        {
            g.setColour (theme::textFaint);
            g.setFont (theme::font (12.5f));
            g.drawFittedText ("Logs: " + logsDir_, logsTextArea_, juce::Justification::centredRight, 1);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (28, 22);

        auto header = area.removeFromTop (64);
        logoArea_ = header.removeFromLeft (64);
        header.removeFromLeft (16);
        pillArea_ = header.removeFromRight (190).withSizeKeepingCentre (190, 34);
        titleArea_ = header.removeFromTop (36);
        subtitleArea_ = header;
        area.removeFromTop (22);

        auto cardsRow = area.removeFromTop (112);
        const int gap = 16;
        const int cardW = (cardsRow.getWidth() - 2 * gap) / 3;
        for (int i = 0; i < 3; ++i)
        {
            cards_[i] = cardsRow.removeFromLeft (cardW);
            cardsRow.removeFromLeft (gap);
        }
        area.removeFromTop (18);

        auto footer = area.removeFromBottom (40);
        toggle_.setBounds (footer.removeFromLeft (210));
        footer.removeFromLeft (10);
        scan_.setBounds (footer.removeFromLeft (160));
        footer.removeFromLeft (10);
        logs_.setBounds (footer.removeFromLeft (190));
        footer.removeFromLeft (12);
        logsTextArea_ = footer;
        area.removeFromBottom (18);

        devicesCard_ = area;
        auto inner = area.reduced (1);
        devicesTitle_ = inner.removeFromTop (48).reduced (18, 0);
        table_.setBounds (inner.reduced (8, 0).withTrimmedBottom (8));
    }

private:
    int getNumRows() override { return static_cast<int> (rows_.size()); }

    void paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected) override
    {
        if (selected)
            g.fillAll (theme::accentSoft);
        else if (row % 2 == 1)
            g.fillAll (theme::surfaceRaised.withAlpha (0.5f));
        g.setColour (theme::outline.withAlpha (0.5f));
        g.fillRect (0, height - 1, width, 1);
    }

    void paintCell (juce::Graphics& g, int row, int column, int width, int height, bool) override
    {
        if (row < 0 || row >= static_cast<int> (rows_.size()))
            return;
        const auto& e = rows_[static_cast<size_t> (row)];
        const auto cell = juce::Rectangle<int> (width, height).reduced (10, 0);

        if (column == 4)
        {
            const bool blocked = e.exclusive == "blocked", allowed = e.exclusive == "allowed";
            const auto colour = blocked ? theme::ok : allowed ? theme::danger : theme::warning;
            const auto label = blocked ? juce::String ("Bloqueado") : allowed ? juce::String ("Permitido") : juce::String ("Desconhecido");
            theme::paintPill (g, cell.toFloat().withSizeKeepingCentre ((float) cell.getWidth(), 22.0f).withWidth (118.0f), colour, label);
            return;
        }

        juce::String value;
        auto colour = theme::text;
        auto f = theme::font (14.0f);
        switch (column)
        {
            case 1:
                value = e.name;
                f = theme::font (14.0f, e.isDefault);
                break;
            case 2: value = flowText (e.flow); colour = theme::textDim; break;
            case 3:
                value = stateText (e.state);
                colour = e.state == EndpointState::active ? theme::text : theme::textFaint;
                break;
            case 5: value = e.format.isNotEmpty() ? e.format : juce::String (utf8 ("—")); colour = theme::textDim; break;
            default: break;
        }
        g.setColour (colour);
        g.setFont (f);
        g.drawText (value, cell, juce::Justification::centredLeft, true);
        if (column == 1 && e.isDefault)
        {
            const float nameW = juce::GlyphArrangement::getStringWidth (f, value);
            const auto badge = juce::Rectangle<float> (juce::jmin ((float) cell.getX() + nameW + 10.0f, (float) cell.getRight() - 52.0f),
                                                       (float) height * 0.5f - 9.0f, 52.0f, 18.0f);
            g.setColour (theme::accentSoft);
            g.fillRoundedRectangle (badge, 9.0f);
            g.setColour (theme::accentLight);
            g.setFont (theme::font (11.0f, true));
            g.drawText (utf8 ("PADRÃO"), badge, juce::Justification::centred, false);
        }
    }

    Actions actions_;
    juce::Image logo_;
    juce::TableListBox table_;
    juce::TextButton toggle_ { "Pausar monitoramento" }, scan_ { "Verificar agora" }, logs_ { "Abrir pasta de logs" };
    std::vector<EndpointStatus> rows_;
    Card exclusive_, format_, activity_;
    juce::String stateText_, modeText_, logsDir_;
    juce::Colour stateColour_ = theme::textDim;
    juce::Rectangle<int> logoArea_, titleArea_, subtitleArea_, pillArea_, cards_[3], devicesCard_, devicesTitle_, logsTextArea_;
    bool paused_ = false;
};

StatusWindow::StatusWindow (Actions actions, std::function<void()> onClose)
    : juce::DocumentWindow ("Audioslave", theme::background, juce::DocumentWindow::allButtons),
      onClose_ (std::move (onClose))
{
    setUsingNativeTitleBar (false);
    setTitleBarHeight (40);
    setTitleBarTextCentred (false);
    setDropShadowEnabled (true);
    setContentOwned (new Content (std::move (actions)), true);
    setResizable (true, true);
    setResizeLimits (760, 560, 10000, 10000);
    setIcon (TrayIcon::logoImage (false, 64));
    centreWithSize (getWidth(), getHeight());

    // Last position, size and maximised state (kept on a visible display by
    // JUCE).
    const auto saved = openUiSettings()->getValue (windowStateKey);
    if (saved.isNotEmpty())
        restoreWindowStateFromString (saved);
}

StatusWindow::~StatusWindow()
{
    saveWindowState();
    clearContentComponent();
}

void StatusWindow::saveWindowState()
{
    auto settings = openUiSettings();
    settings->setValue (windowStateKey, getWindowStateAsString());
    settings->saveIfNeeded();
}

void StatusWindow::resized()
{
    juce::DocumentWindow::resized();
    // The maximise button shows "restore" while the window is maximised.
    if (auto* maximise = getMaximiseButton())
        maximise->repaint();
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

bool StatusWindow::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        closeButtonPressed();
        return true;
    }
    return juce::DocumentWindow::keyPressed (key);
}
} // namespace audioslave
