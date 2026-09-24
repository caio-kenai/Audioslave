#pragma once
// "Configurações" page of the Audioslave window: the audio settings that the
// installer sets initially, changeable at any time without reinstalling.
//
// Applying never writes config.ini directly (it is read-only for users): the
// service is asked first for a preview (ANALYZE), the user confirms whatever
// deserves it (bit-depth limits, devices without the rate, devices that will
// be disabled), then CONFIGURE saves, reloads and applies the settings at once
// and returns what happened to each device.

#include "ipc/ControlClient.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace audioslave
{
class SettingsView final : public juce::Component
{
public:
    using Request = std::function<void (ipc::Command, const juce::var& args, std::function<void (const ipc::Reply&)>)>;

    SettingsView (Request request, std::function<void()> onBack);
    ~SettingsView() override;

    // Latest status from the service (nullptr: not connected). Controls the
    // user has not touched follow it.
    void update (const ipc::StatusSnapshot* status);

    // Back to the saved values.
    void revert();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ipc::AudioSettings chosen() const;
    void setControls (const ipc::AudioSettings& settings);
    void updateEnabledState();
    void apply();
    void preview (const ipc::AudioSettings& settings, const std::vector<DeviceReport>& devices);
    void configure (ipc::AudioSettings settings);
    void setBusy (bool busy, const juce::String& text = {});

    Request request_;
    std::function<void()> onBack_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool> (true);
    std::optional<ipc::AudioSettings> saved_;
    bool savedConfirmed_ = false; // the service's policy is confirmed for the saved format
    bool connected_ = false;
    bool dirty_ = false;
    bool busy_ = false;

    juce::Rectangle<int> card_;
    juce::Label title_, subtitle_, section_, exclusiveNote_, formatNote_, rateLabel_, bitsLabel_, disableNote_, status_;
    juce::ToggleButton exclusive_, format_, disable_;
    juce::ComboBox rate_, bits_;
    juce::TextButton back_, apply_, cancel_;
};
} // namespace audioslave
