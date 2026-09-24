#include "installer/SetupOptions.h"
#include "config/Configuration.h"

namespace audioslave::setup
{
Options parseOptions (const juce::StringArray& arguments, const juce::String& executableName)
{
    Options opt;
    for (const auto& raw : arguments)
    {
        const auto a = raw.toLowerCase();
        if (a == "/s" || a == "/silent" || a == "-s")
            opt.silent = true;
        else if (a == "/uninstall")
            opt.uninstall = true;
        else if (a == "/uninstall-stage2")
            opt.uninstall = opt.uninstallStage2 = true;
        else if (a == "/removedata")
            opt.removeData = true;
        else if (a == "/notray")
            opt.launchTray = false;
        else if (a == "/disableincompatible")
            opt.disableSet = opt.disableIncompatible = true;
        else if (a == "/noformat")
        {
            opt.formatSet = true;
            opt.format = false;
        }
        else if (a.startsWith ("/format="))
        {
            const auto value = raw.fromFirstOccurrenceOf ("=", false, false);
            const auto rate = static_cast<std::uint64_t> (value.upToFirstOccurrenceOf (":", false, false).getLargeIntValue());
            const auto bits = value.contains (":")
                                  ? static_cast<std::uint64_t> (value.fromFirstOccurrenceOf (":", false, false).getLargeIntValue())
                                  : static_cast<std::uint64_t> (defaultBitDepth);
            opt.formatSet = true;
            opt.format = true;
            opt.sampleRate = isSupportedSampleRate (rate) ? static_cast<std::uint32_t> (rate) : defaultSampleRate;
            opt.bitDepth = isSupportedBitDepth (bits) ? static_cast<std::uint16_t> (bits) : defaultBitDepth;
        }
        else if (a.startsWith ("/dir="))
        {
            opt.dir = raw.fromFirstOccurrenceOf ("=", false, false).unquoted().trim();
            while (opt.dir.endsWithChar ('\\') || opt.dir.endsWithChar ('"'))
                opt.dir = opt.dir.dropLastCharacters (1);
        }
    }
    if (executableName.equalsIgnoreCase ("Uninstall.exe"))
        opt.uninstall = true;
    return opt;
}
} // namespace audioslave::setup
