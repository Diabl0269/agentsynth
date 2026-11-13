// Concern: the Audio tab's Device section -- the interface, sample rate and buffer size.
#include "UI/Settings/AudioSettingsTab/AudioDeviceSection.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Settings/AudioSettingsTab/AudioBluetooth.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace {
constexpr int kAdvancedToggleWidth = 110;
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
    isBluetooth = [](const juce::String& deviceName) { return synth::audio::isBluetoothDevice(deviceName); };
    for (auto* box : {&output_, &input_})
        addChildComponent(*box);
    for (auto* box : {&type_, &rate_, &buffer_})
        advanced_.addChildComponent(*box);
    addChildComponent(test_);
    advanced_.addChildComponent(controlPanel_);
    advancedToggle_.setComponentID("audioAdvancedToggle");
    addAndMakeVisible(advancedToggle_);
    addChildComponent(advanced_);
    for (auto* note : {&bluetoothNote_, &micWarning_}) {
        note->setFont(juce::Font(juce::FontOptions(11.5f)));
        note->setMinimumHorizontalScale(1.0f);
        note->setJustificationType(juce::Justification::topLeft);
        note->setInterceptsMouseClicks(false, false);
        addChildComponent(*note);
    }
    bluetoothNote_.setText("Bluetooth output: it runs at a fixed sample rate and adds delay, so playing live feels "
                           "late. A wired output is faster.",
                           juce::dontSendNotification);
    micWarning_.setText("This Bluetooth microphone drops the headphones into a low-quality call mode. Pick the "
                        "built-in microphone as the input instead.",
                        juce::dontSendNotification);
    advancedHint_.setText("Choose an output device to see its sample rate and buffer size.",
                          juce::dontSendNotification);
    advancedHint_.setFont(juce::Font(juce::FontOptions(11.5f)));
    advancedHint_.setJustificationType(juce::Justification::centredLeft);
    advancedHint_.setInterceptsMouseClicks(false, false);
    advanced_.addChildComponent(advancedHint_);
    bluetoothNote_.setTitle("Bluetooth output note");
    micWarning_.setTitle("Bluetooth microphone warning");
    advancedFade_.onFrame = [this] {
        if (onLayoutChanged)
            onLayoutChanged();
    };
    advancedToggle_.onOpenRequested = [this](bool open) {
        setAdvancedOpen(open);
        if (onAdvancedToggled)
            onAdvancedToggled(open);
    };
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
    const auto previous = deviceManager_.getAudioDeviceSetup();
    const auto remembered = source_.layoutStore().deviceSettings(deviceManager_.getCurrentAudioDeviceType(), chosen);
    const auto error = openPicked(setup, remembered, isInput);
    if (error.isNotEmpty()) {
        deviceManager_.setAudioDeviceSetup(previous, true);
        if (onOpenFailed)
            onOpenFailed(chosen, error);
        else
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Audio device",
                                                   "Could not open \"" + chosen + "\": " + error);
    }
    populate();
}

// A different device does not share the old device's rate, buffer size or output channels (a Bluetooth output in
// particular), so an output pick opens with what the user last had on that device, or with the device's own defaults
// (0) when it is new or the remembered values no longer open. An input pick leaves the output channels as they are.
// Returns JUCE's error, empty on success.
juce::String AudioDeviceSection::openPicked(juce::AudioDeviceManager::AudioDeviceSetup setup,
                                            const synth::AudioInputLayoutStore::DeviceSettings& remembered,
                                            bool isInput) {
    auto defaults = setup;
    defaults.sampleRate = 0.0;
    defaults.bufferSize = 0;
    if (!isInput)
        defaults.useDefaultOutputChannels = true;
    if (!isInput && remembered.outputMask != 0 && remembered.sampleRate > 0.0 && remembered.bufferSize > 0) {
        setup.sampleRate = remembered.sampleRate;
        setup.bufferSize = remembered.bufferSize;
        setup.outputChannels.clear();
        setup.outputChannels.setBitRangeAsInt(0, 32, remembered.outputMask);
        setup.useDefaultOutputChannels = false;
        if (deviceManager_.setAudioDeviceSetup(setup, true).isEmpty())
            return {};
    }
    return deviceManager_.setAudioDeviceSetup(defaults, true);
}

// The rows under Advanced that have something to show.
int AudioDeviceSection::advancedRows() const {
    int rows = 0;
    for (auto* box : {&type_, &rate_, &buffer_})
        rows += box->isVisible() ? 1 : 0;
    return rows + (advancedHint_.isVisible() ? 1 : 0);
}

int AudioDeviceSection::noteHeight(const juce::Label& note, int width) const {
    if (!note.isVisible())
        return 0;
    return wrappedHeight(note.getText(), note.getFont(), width - 2 * kPad - kLabelWidth) + 4;
}

// The Advanced rows take their height from the fade, so the sections below slide with it.
int AudioDeviceSection::preferredHeight(int width) const {
    const int outputRows = (output_.isVisible() ? 1 : 0) + (input_.isVisible() ? 1 : 0);
    const int advancedHeight = juce::roundToInt((float)(advancedRows() * kRowHeight) * advancedFade_.progress());
    return kPad * 2 + juce::jmax(outputRows, 1) * kRowHeight + noteHeight(bluetoothNote_, width) +
           noteHeight(micWarning_, width) + kRowHeight + advancedHeight;
}

void AudioDeviceSection::setAdvancedOpen(bool open, bool animate) {
    advancedToggle_.setOpen(open, animate);
    if (animate)
        advancedFade_.setShown(open);
    else
        advancedFade_.snapTo(open);
    if (onLayoutChanged)
        onLayoutChanged();
    resized();
}

void AudioDeviceSection::lookAndFeelChanged() {
    const auto& colours = synth::theme::themeOf(*this).colors;
    bluetoothNote_.setColour(juce::Label::textColourId, colours.textMuted);
    advancedHint_.setColour(juce::Label::textColourId, colours.textMuted);
    micWarning_.setColour(juce::Label::textColourId, colours.warning);
}

// A Bluetooth output is slower and fixed-rate; a Bluetooth microphone switches the headphones to the call profile.
void AudioDeviceSection::updateNotes(const juce::AudioDeviceManager::AudioDeviceSetup& setup, bool separate) {
    const auto bluetooth = [this](const juce::String& deviceName) {
        return deviceName.isNotEmpty() && isBluetooth && isBluetooth(deviceName);
    };
    bluetoothNote_.setVisible(output_.isVisible() && bluetooth(setup.outputDeviceName));
    micWarning_.setVisible(separate && input_.isVisible() && bluetooth(setup.inputDeviceName));
    lookAndFeelChanged();
}

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
    updateNotes(setup, separate);

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
    advancedHint_.setVisible(device == nullptr);
    resized();
}

void AudioDeviceSection::refresh() { populate(); }

void AudioDeviceSection::resized() {
    auto area = getLocalBounds().reduced(kPad);
    const int buttonWidth = 100;
    const int noteIndent = kLabelWidth;
    const auto placeNote = [&](juce::Label& note) {
        if (!note.isVisible())
            return;
        auto row = area.removeFromTop(noteHeight(note, getWidth()));
        note.setBounds(row.withTrimmedLeft(noteIndent));
    };
    if (output_.isVisible()) {
        auto row = area.removeFromTop(kRowHeight);
        row.removeFromLeft(kLabelWidth);
        if (test_.isVisible()) {
            test_.setBounds(row.removeFromRight(buttonWidth).reduced(0, 2));
            row.removeFromRight(kPad);
        }
        output_.setBounds(row.reduced(0, 2));
        placeNote(bluetoothNote_);
    }
    if (input_.isVisible()) {
        auto row = area.removeFromTop(kRowHeight);
        row.removeFromLeft(kLabelWidth);
        input_.setBounds(row.reduced(0, 2));
        placeNote(micWarning_);
    }
    advancedToggle_.setBounds(area.removeFromTop(kRowHeight).removeFromLeft(kAdvancedToggleWidth));
    // Full height: the section's own bounds clip it while the fade is part way.
    const int rows = advancedRows();
    advanced_.setBounds(area.removeFromTop(rows * kRowHeight));
    auto inner = advanced_.getLocalBounds();
    for (auto* box : {&type_, &rate_, &buffer_}) {
        if (!box->isVisible())
            continue;
        auto row = inner.removeFromTop(kRowHeight);
        row.removeFromLeft(kLabelWidth);
        if (box == &buffer_ && controlPanel_.isVisible()) {
            controlPanel_.setBounds(row.removeFromRight(buttonWidth + 20).reduced(0, 2));
            row.removeFromRight(kPad);
        }
        box->setBounds(row.reduced(0, 2));
    }
    if (advancedHint_.isVisible())
        advancedHint_.setBounds(inner.removeFromTop(kRowHeight).withTrimmedLeft(kLabelWidth));
}
