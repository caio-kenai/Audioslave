#include "app/SettingsReport.h"
#include "common/Strings.h"
#include "tests/Mocks.h"

namespace audioslave::test
{
class SettingsReportTests final : public juce::UnitTest
{
public:
    SettingsReportTests() : juce::UnitTest ("Settings report", "App") {}

    static DeviceReport device (const char* name, Compatibility c, DeviceAction a, const char* caps)
    {
        DeviceReport d;
        d.id = name;
        d.name = name;
        d.compatibility = c;
        d.action = a;
        d.capabilities = FormatCapabilities::deserialise (caps);
        return d;
    }

    static ipc::AudioSettings settings (bool disable)
    {
        ipc::AudioSettings s;
        s.formatStandardization = true;
        s.sampleRate = 48000;
        s.bitDepth = 24;
        s.disableIncompatibleDevices = disable;
        return s;
    }

    void runTest() override
    {
        // Devices A and B support 48000 / 24, C only 48000 / 16, HDMI lacks 48000 Hz.
        const std::vector<DeviceReport> ignoreList {
            device ("Dispositivo A", Compatibility::compatible, DeviceAction::apply, "48000:16/24"),
            device ("Dispositivo B", Compatibility::compatible, DeviceAction::compliant, "48000:24"),
            device ("Dispositivo C", Compatibility::depthUnsupported, DeviceAction::ignore, "48000:16"),
            device ("HDMI", Compatibility::rateUnsupported, DeviceAction::ignore, "44100:16"),
        };

        beginTest ("Limitations are described in the user's words");
        {
            expectEquals (describeLimitation (ignoreList[2], 48000, 24), utf8 ("48000 Hz disponível, máximo de 16 bits"));
            expectEquals (describeLimitation (ignoreList[3], 48000, 24), utf8 ("48000 Hz não disponível (suporta 44100 Hz)"));
        }

        beginTest ("Bit-depth limit: alert lists the limited device and asks to continue");
        {
            const auto p = buildPreview (settings (false), ignoreList);
            expect (p.needsConfirmation);
            expect (! p.disablesDevices);
            expectEquals (p.title, utf8 ("Limitação de profundidade de bits"));
            expect (p.message.contains ("48000 Hz / 24 bits"));
            expect (p.details.contains (utf8 ("Limitados pela profundidade de bits (1)")));
            expect (p.details.contains (utf8 ("Dispositivo C\n      48000 Hz disponível, máximo de 16 bits")));
            expect (p.details.contains (utf8 ("Sem a taxa de amostragem (1)")));
            expect (p.details.contains (utf8 ("Serão ignorados (continuam habilitados) (2)")));
            expect (p.details.contains (utf8 ("Compatíveis (2)")));
        }

        beginTest ("Policy on: the devices to be disabled are listed before anything happens");
        {
            auto list = ignoreList;
            list[2].action = DeviceAction::disable;
            list[3].action = DeviceAction::disable;
            const auto p = buildPreview (settings (true), list);
            expect (p.needsConfirmation);
            expect (p.disablesDevices);
            expect (p.details.contains (utf8 ("Serão desabilitados (2)")));
            expect (! p.details.contains ("ignorados"));
        }

        beginTest ("Everything compatible: no dialog");
        {
            const std::vector<DeviceReport> fine { ignoreList[0], ignoreList[1] };
            expect (! buildPreview (settings (false), fine).needsConfirmation);
        }

        beginTest ("Result: what could not be configured, and why");
        {
            auto result = ignoreList;
            result[0].action = DeviceAction::applied;
            const auto o = buildOutcome (settings (false), result, false);
            expect (o.problems);
            expectEquals (o.title, utf8 ("Configuração aplicada"));
            expect (o.message.contains (utf8 ("48000 Hz / 24 bits em 2 dispositivo(s)")));
            expect (o.details.contains (utf8 ("Não foi possível aplicar a configuração (2)")));
            expect (o.details.contains (utf8 ("HDMI\n      Motivo: 48000 Hz não disponível")));
            expect (o.details.contains (utf8 ("Dispositivo C\n      Motivo: 48000 Hz disponível, máximo de 16 bits")));
        }

        beginTest ("Result: disabled devices and failures are reported");
        {
            auto result = ignoreList;
            result[2].action = DeviceAction::disabled;
            result[3].action = DeviceAction::failed;
            result[3].reason = "could not be disabled (0x80070005)";
            const auto o = buildOutcome (settings (true), result, false);
            expectEquals (o.title, utf8 ("Configuração aplicada com falhas"));
            expect (o.details.contains (utf8 ("Desabilitados por não suportarem a configuração (1)")));
            expect (o.details.contains (utf8 ("o Windows não desabilitou o dispositivo")));
            expect (o.message.contains (utf8 ("1 dispositivo(s) desabilitado(s)")));
        }

        beginTest ("Result while paused: saved, applied on resume");
        {
            const auto o = buildOutcome (settings (false), {}, true);
            expect (o.paused);
            expectEquals (o.title, utf8 ("Configuração salva"));
        }
    }
};

static SettingsReportTests settingsReportTests;
} // namespace audioslave::test
