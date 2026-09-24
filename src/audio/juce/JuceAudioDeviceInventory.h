#pragma once
// The audio devices exactly as a JUCE application sees them (WASAPI shared
// mode): the names a JUCE player - such as the ones Playlist builds - uses to
// pick its output. Shown by `AudioslaveService devices`.
//
// Needs a JUCE message manager on the calling thread
// (juce::ScopedJuceInitialiser_GUI) and COM initialised.

#include <juce_audio_devices/juce_audio_devices.h>

namespace audioslave
{
struct JuceDeviceList
{
    juce::String typeName;
    juce::StringArray outputs;
    juce::StringArray inputs;
    bool available = false;
};

JuceDeviceList scanJuceWasapiDevices (juce::WASAPIDeviceMode mode = juce::WASAPIDeviceMode::shared);
} // namespace audioslave
