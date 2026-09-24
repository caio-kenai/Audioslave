#include "app/SettingsReport.h"
#include "common/Strings.h"

namespace audioslave
{
namespace
{
juce::String target (const ipc::AudioSettings& s)
{
    return juce::String (s.sampleRate) + " Hz / " + juce::String (s.bitDepth) + " bits";
}

juce::String bullet (const DeviceReport& d, const juce::String& detail = {})
{
    return utf8 ("  • ") + d.name + (detail.isNotEmpty() ? utf8 ("\n      ") + detail : juce::String());
}

void section (juce::String& out, const juce::String& heading, const juce::StringArray& lines)
{
    if (lines.isEmpty())
        return;
    if (out.isNotEmpty())
        out << "\n\n";
    out << heading << " (" << lines.size() << ")\n" << lines.joinIntoString ("\n");
}

bool isIncompatible (const DeviceReport& d)
{
    return d.compatibility == Compatibility::rateUnsupported || d.compatibility == Compatibility::depthUnsupported;
}

juce::String failureReason (const DeviceReport& d)
{
    // The service's reasons are English (log); the common cases get Portuguese text.
    if (d.reason.contains ("did not stick"))
        return utf8 ("a alteração não foi mantida pelo driver");
    if (d.reason.contains ("rejected"))
        return utf8 ("o driver recusou o formato");
    if (d.reason.contains ("could not be disabled"))
        return utf8 ("o Windows não desabilitou o dispositivo");
    if (d.reason.contains ("could not be enabled"))
        return utf8 ("o Windows não reativou o dispositivo");
    return d.reason;
}
} // namespace

juce::String flowLabel (EndpointFlow flow)
{
    return flow == EndpointFlow::capture ? utf8 ("Captura") : utf8 ("Reprodução");
}

juce::String describeLimitation (const DeviceReport& d, std::uint32_t rate, std::uint16_t bits)
{
    switch (d.compatibility)
    {
        case Compatibility::compatible:
            return juce::String (rate) + utf8 (" Hz / ") + juce::String (bits) + utf8 (" bits disponível");
        case Compatibility::rateUnsupported:
        {
            const auto rates = d.capabilities.rates();
            juce::StringArray list;
            for (auto r : rates)
                list.add (juce::String (r));
            return juce::String (rate) + utf8 (" Hz não disponível")
                   + (list.isEmpty() ? juce::String() : utf8 (" (suporta ") + list.joinIntoString (", ") + " Hz)");
        }
        case Compatibility::depthUnsupported:
            return juce::String (rate) + utf8 (" Hz disponível, máximo de ") + juce::String (d.capabilities.maxDepthAt (rate))
                   + " bits";
        case Compatibility::unknown:
            break;
    }
    return utf8 ("formatos suportados desconhecidos (o driver não informa)");
}

SettingsPreview buildPreview (const ipc::AudioSettings& settings, const std::vector<DeviceReport>& devices)
{
    SettingsPreview p;
    juce::StringArray compatible, limited, missingRate, unknown, reenabled, disabled, ignored;
    for (const auto& d : devices)
    {
        switch (d.action)
        {
            case DeviceAction::compliant:
            case DeviceAction::apply:
                compatible.add (bullet (d));
                break;
            case DeviceAction::reenable:
                reenabled.add (bullet (d, utf8 ("desabilitado antes pelo Audioslave; será reativado")));
                break;
            case DeviceAction::unknown:
                unknown.add (bullet (d, describeLimitation (d, settings.sampleRate, settings.bitDepth)));
                break;
            default:
                break;
        }
        if (isIncompatible (d))
        {
            const auto detail = describeLimitation (d, settings.sampleRate, settings.bitDepth);
            (d.compatibility == Compatibility::depthUnsupported ? limited : missingRate).add (bullet (d, detail));
            const bool willDisable = d.action == DeviceAction::disable || d.action == DeviceAction::keepDisabled;
            (willDisable ? disabled : ignored).add (bullet (d));
        }
    }

    if (! settings.formatStandardization)
    {
        p.title = utf8 ("Desativar a padronização de formato?");
        p.message = utf8 ("Os dispositivos deixam de ser ajustados para uma taxa de amostragem e profundidade fixas.");
        section (p.details, utf8 ("Serão reativados"), reenabled);
        p.needsConfirmation = ! reenabled.isEmpty();
        return p;
    }

    const bool anyLimited = ! limited.isEmpty();
    p.disablesDevices = ! disabled.isEmpty() && settings.disableIncompatibleDevices;
    p.needsConfirmation = anyLimited || ! missingRate.isEmpty() || p.disablesDevices || ! unknown.isEmpty()
                          || ! reenabled.isEmpty();

    section (p.details, utf8 ("Compatíveis"), compatible);
    section (p.details, utf8 ("Limitados pela profundidade de bits"), limited);
    section (p.details, utf8 ("Sem a taxa de amostragem"), missingRate);
    section (p.details, utf8 ("Formatos desconhecidos (não serão alterados)"), unknown);
    if (settings.disableIncompatibleDevices)
        section (p.details, utf8 ("Serão desabilitados"), disabled);
    else
        section (p.details, utf8 ("Serão ignorados (continuam habilitados)"), ignored);
    section (p.details, utf8 ("Serão reativados"), reenabled);

    if (p.disablesDevices)
    {
        p.title = utf8 ("Alguns dispositivos serão desabilitados");
        p.message = utf8 ("Alguns dispositivos de áudio não suportam ") + target (settings)
                    + utf8 (". A opção atual desabilita os dispositivos incompatíveis: os listados abaixo serão "
                            "desabilitados no Windows (podem ser reativados depois pelo Audioslave).");
    }
    else if (anyLimited)
    {
        p.title = utf8 ("Limitação de profundidade de bits");
        p.message = utf8 ("Alguns dispositivos suportam a taxa de amostragem selecionada, mas possuem limitação de "
                          "profundidade de bits. Configuração selecionada: ")
                    + target (settings) + utf8 (". Deseja continuar mesmo assim?");
    }
    else if (! missingRate.isEmpty())
    {
        p.title = utf8 ("Dispositivos sem a taxa de amostragem");
        p.message = utf8 ("Alguns dispositivos não suportam ") + juce::String (settings.sampleRate)
                    + utf8 (" Hz e não receberão a configuração. Deseja continuar?");
    }
    else
    {
        p.title = utf8 ("Aplicar ") + target (settings) + "?";
        p.message = utf8 ("Confira o que acontecerá com cada dispositivo.");
    }
    return p;
}

SettingsOutcome buildOutcome (const ipc::AudioSettings& settings, const std::vector<DeviceReport>& devices, bool paused)
{
    SettingsOutcome o;
    o.paused = paused;
    juce::StringArray applied, failed, notConfigured, disabled, reenabled;
    for (const auto& d : devices)
    {
        switch (d.action)
        {
            case DeviceAction::applied:
            case DeviceAction::compliant:
                applied.add (bullet (d));
                break;
            case DeviceAction::failed:
                failed.add (bullet (d, utf8 ("Motivo: ") + failureReason (d)));
                break;
            case DeviceAction::disabled:
            case DeviceAction::keepDisabled:
                disabled.add (bullet (d, utf8 ("Motivo: ") + describeLimitation (d, settings.sampleRate, settings.bitDepth)));
                break;
            case DeviceAction::reenabled:
                reenabled.add (bullet (d));
                break;
            case DeviceAction::ignore:
            case DeviceAction::pending:
            case DeviceAction::leftEnabled:
            case DeviceAction::unknown:
                notConfigured.add (bullet (d, utf8 ("Motivo: ") + describeLimitation (d, settings.sampleRate, settings.bitDepth)));
                break;
            default:
                break;
        }
    }

    section (o.details, utf8 ("Não foi possível aplicar a configuração"), notConfigured);
    section (o.details, utf8 ("Falhas"), failed);
    section (o.details, utf8 ("Desabilitados por não suportarem a configuração"), disabled);
    section (o.details, utf8 ("Reativados"), reenabled);
    o.problems = ! failed.isEmpty() || ! notConfigured.isEmpty() || ! disabled.isEmpty();

    if (paused)
    {
        o.title = utf8 ("Configuração salva");
        o.message = utf8 ("O monitoramento está pausado: a configuração será aplicada ao retomar.");
        return o;
    }
    o.title = failed.isEmpty() ? utf8 ("Configuração aplicada") : utf8 ("Configuração aplicada com falhas");
    o.message = settings.formatStandardization
                    ? target (settings) + utf8 (" em ") + juce::String (applied.size()) + utf8 (" dispositivo(s).")
                    : utf8 ("Padronização de formato desativada.");
    if (! disabled.isEmpty())
        o.message << utf8 (" ") << juce::String (disabled.size())
                  << utf8 (" dispositivo(s) desabilitado(s) por não suportarem a configuração.");
    return o;
}
} // namespace audioslave
