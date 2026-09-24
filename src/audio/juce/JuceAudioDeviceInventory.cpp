#include "audio/juce/JuceAudioDeviceInventory.h"

#include <memory>

namespace audioslave
{
JuceDeviceList scanJuceWasapiDevices (juce::WASAPIDeviceMode mode)
{
    JuceDeviceList list;
    std::unique_ptr<juce::AudioIODeviceType> type (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (mode));
    if (type == nullptr)
        return list;

    type->scanForDevices();
    list.available = true;
    list.typeName = type->getTypeName();
    list.outputs = type->getDeviceNames (false);
    list.inputs = type->getDeviceNames (true);
    return list;
}
} // namespace audioslave
