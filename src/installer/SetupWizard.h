#pragma once
// Installer wizard (JUCE components). The installation itself runs on a
// worker thread (InstallerCore); progress comes back on the message thread.

#include "app/Theme.h"
#include "installer/SetupOptions.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace audioslave::setup
{
class SetupWizard final : public juce::Component
{
public:
    // Called with the process exit code when the user closes the wizard.
    SetupWizard (Options options, std::function<void (int exitCode)> onFinished);
    ~SetupWizard() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Window close button.
    void requestClose();

private:
    void startInstall();
    void browse();
    void updateFormatControls();
    // With format standardization: previews every device and asks first when
    // something deserves a decision (as the Configurações screen does).
    void confirmThenInstall();
    void finished (bool ok, const juce::String& error);

    Options options_;
    std::function<void (int)> onFinished_;
    juce::Image logo_;
    juce::Rectangle<int> featuresCard_, locationCard_;

    juce::Label title_, subtitle_, featuresHeader_, exclusiveNote_, rateLabel_, bitsLabel_, formatNote_, disableNote_, dirLabel_,
        status_;
    juce::ToggleButton exclusive_, format_, disable_, launch_;
    juce::ComboBox rate_, bits_;
    juce::TextEditor dir_;
    juce::TextButton browse_ { "Procurar..." }, install_ { "Instalar" }, cancel_ { "Cancelar" };
    double progressValue_ = 0.0;
    juce::ProgressBar progress_ { progressValue_ };
    std::unique_ptr<juce::FileChooser> chooser_;

    struct Worker;
    std::unique_ptr<Worker> worker_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>> (true);
    bool running_ = false;
    bool done_ = false;
    bool succeeded_ = false;
};
} // namespace audioslave::setup
