// Concern: the Audio tab's Device section -- the interface, sample rate and buffer size.
#include "UI/Settings/AudioSettingsTab/AudioDeviceSection.h"

#include "UI/Layout/FocusRing.h"

namespace {
juce::String dot() { return juce::String::fromUTF8(" \xC2\xB7 "); }

void name(juce::ComboBox& box, juce::Label& label, const juce::String& title) {
    label.setText(title, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.attachToComponent(&box, true);
    box.setTitle(title);
    box.setTooltip(title);
    box.setWantsKeyboardFocus(true);
}
} // namespace

AudioDeviceSection::AudioDeviceSection(juce::AudioDeviceManager& deviceManager, AudioSettingsSource& source)
    : deviceManager_(deviceManager)
    , source_(source) {
    for (auto* box : {&type_, &output_, &input_, &rate_, &buffer_})
        addChildComponent(*box);
    addChildComponent(test_);
    addChildComponent(controlPanel_);
    name(type_, typeLabel_, "Audio device type");
    name(output_, outputLabel_, "Output");
    name(input_, inputLabel_, "Input");
    name(rate_, rateLabel_, "Sample rate");
    name(buffer_, bufferLabel_, "Buffer size");
    for (auto* label : {&typeLabel_, &outputLabel_, &inputLabel_, &rateLabel_, &bufferLabel_})
        label->setVisible(false);
    test_.setTitle("Test");
    test_.setTooltip("Plays a test tone on the output");
    test_.onClick = [this] { deviceManager_.playTestSound(); };
    controlPanel_.setTitle("Control panel");
    controlPanel_.setTooltip("Opens the interface's own control panel");
    controlPanel_.onClick = [this] {
        if (auto* device = deviceManager_.getCurrentAudioDevice())
            device->showControlPanel();
    };

    type_.onChange = [this] {
        if (populating_)
            return;
        if (auto* chosen = deviceManager_.getAvailableDeviceTypes()[type_.getSelectedId() - 1])
            deviceManager_.setCurrentAudioDeviceType(chosen->getTypeName(), true);
    };
    output_.onChange = [this] { applyDeviceChoice(false); };
    input_.onChange = [this] { applyDeviceChoice(true); };
    rate_.onChange = [this] {
        if (populating_)
            return;
        auto setup = deviceManager_.getAudioDeviceSetup();
        setup.sampleRate = rate_.getSelectedId();
        deviceManager_.setAudioDeviceSetup(setup, true);
    };
    buffer_.onChange = [this] {
        if (populating_)
            return;
        auto setup = deviceManager_.getAudioDeviceSetup();
        setup.bufferSize = buffer_.getSelectedId();
        deviceManager_.setAudioDeviceSetup(setup, true);
    };
    populate();
}

// A device pick keeps the inputs the user had on (or none, which keeps the app's inputs-off default): a combined
// interface carries its input device along, and a separate input device stays what it was.
void AudioDeviceSection::applyDeviceChoice(bool isInput) {
    if (populating_)
        return;
    auto* current = deviceManager_.getCurrentDeviceTypeObject();
    auto& box = isInput ? input_ : output_;
    if (current == nullptr || box.getSelectedId() == 0)
        return;
    const auto chosen = box.getText();
    auto setup = deviceManager_.getAudioDeviceSetup();
    const bool inputsOn = !setup.inputChannels.isZero();
    if (isInput) {
        setup.inputDeviceName = box.getSelectedId() < 0 ? juce::String() : chosen;
    } else {
        setup.outputDeviceName = box.getSelectedId() < 0 ? juce::String() : chosen;
        if (!current->hasSeparateInputsAndOutputs())
            setup.inputDeviceName = inputsOn ? setup.outputDeviceName : juce::String();
    }
    deviceManager_.setAudioDeviceSetup(setup, true);
}

int AudioDeviceSection::visibleRows() const {
    int rows = 0;
    for (auto* box : {&type_, &output_, &input_, &rate_, &buffer_})
        rows += box->isVisible() ? 1 : 0;
    return juce::jmax(rows, 1);
}

int AudioDeviceSection::preferredHeight(int) const { return kPad * 2 + visibleRows() * kRowHeight; }

juce::String AudioDeviceSection::summary() const {
    const auto info = source_.currentInterface();
    if (info.deviceName.isEmpty() && info.sampleRate <= 0.0)
        return "No audio device";
    const double ms = info.sampleRate > 0.0 ? 1000.0 * info.bufferSamples / info.sampleRate : 0.0;
    return juce::String(juce::roundToInt(info.sampleRate)) + " Hz" + dot() + juce::String(info.bufferSamples) +
           " samples (" + juce::String(ms, 1) + " ms)" + dot() + info.deviceType;
}

void AudioDeviceSection::populate() {
    const juce::ScopedValueSetter<bool> guard(populating_, true);
    auto* typeObject =
        deviceManager_.getCurrentAudioDeviceType().isEmpty() ? nullptr : deviceManager_.getCurrentDeviceTypeObject();
    const auto& types = deviceManager_.getAvailableDeviceTypes();
    type_.clear(juce::dontSendNotification);
    if (typeObject != nullptr && types.size() > 1) {
        for (int i = 0; i < types.size(); ++i)
            type_.addItem(types.getUnchecked(i)->getTypeName(), i + 1);
        type_.setText(deviceManager_.getCurrentAudioDeviceType(), juce::dontSendNotification);
    }
    type_.setVisible(type_.getNumItems() > 0);
    typeLabel_.setVisible(type_.isVisible());

    const auto setup = deviceManager_.getAudioDeviceSetup();
    const bool separate = typeObject != nullptr && typeObject->hasSeparateInputsAndOutputs();
    for (const auto& pair : {std::pair{&output_, false}, std::pair{&input_, true}}) {
        auto& box = *pair.first;
        box.clear(juce::dontSendNotification);
        const bool shown = typeObject != nullptr && (!pair.second || separate);
        if (shown) {
            const auto names = typeObject->getDeviceNames(pair.second);
            for (int i = 0; i < names.size(); ++i)
                box.addItem(names[i], i + 1);
            box.addItem("No device", -1);
            const auto& chosen = pair.second ? setup.inputDeviceName : setup.outputDeviceName;
            box.setText(chosen.isEmpty() ? juce::String("No device") : chosen, juce::dontSendNotification);
        }
        box.setVisible(shown);
    }
    outputLabel_.setText(separate ? "Output" : "Device", juce::dontSendNotification);
    output_.setTitle(outputLabel_.getText());
    output_.setTooltip(outputLabel_.getText());
    outputLabel_.setVisible(output_.isVisible());
    inputLabel_.setVisible(input_.isVisible());

    auto* device = deviceManager_.getCurrentAudioDevice();
    rate_.clear(juce::dontSendNotification);
    buffer_.clear(juce::dontSendNotification);
    if (device != nullptr) {
        for (const double each : device->getAvailableSampleRates())
            rate_.addItem(juce::String(juce::roundToInt(each)) + " Hz", juce::roundToInt(each));
        rate_.setText(juce::String(juce::roundToInt(device->getCurrentSampleRate())) + " Hz",
                      juce::dontSendNotification);
        const double rate = device->getCurrentSampleRate() > 0.0 ? device->getCurrentSampleRate() : 48000.0;
        for (const int size : device->getAvailableBufferSizes())
            buffer_.addItem(juce::String(size) + " samples (" + juce::String(size * 1000.0 / rate, 1) + " ms)", size);
        buffer_.setSelectedId(device->getCurrentBufferSizeSamples(), juce::dontSendNotification);
    }
    for (auto* box : {&rate_, &buffer_})
        box->setVisible(device != nullptr);
    rateLabel_.setVisible(device != nullptr);
    bufferLabel_.setVisible(device != nullptr);
    test_.setVisible(device != nullptr && output_.isVisible());
    controlPanel_.setVisible(device != nullptr && device->hasControlPanel());
    resized();
}

void AudioDeviceSection::refresh() { populate(); }

void AudioDeviceSection::resized() {
    auto area = getLocalBounds().reduced(kPad);
    const int buttonWidth = 100;
    for (auto* box : {&type_, &output_, &input_, &rate_, &buffer_}) {
        if (!box->isVisible())
            continue;
        auto row = area.removeFromTop(kRowHeight);
        row.removeFromLeft(kLabelWidth);
        if (box == &output_ && test_.isVisible()) {
            test_.setBounds(row.removeFromRight(buttonWidth).reduced(0, 2));
            row.removeFromRight(kPad);
        }
        if (box == &buffer_ && controlPanel_.isVisible()) {
            controlPanel_.setBounds(row.removeFromRight(buttonWidth + 20).reduced(0, 2));
            row.removeFromRight(kPad);
        }
        box->setBounds(row.reduced(0, 2));
    }
}
