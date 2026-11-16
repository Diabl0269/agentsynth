// AddControllerPopover.cpp: see the header.
//
// Combo ids: devices are 1-based indices into devices_; the Start-with combo uses 1 = Detect,
// 2 = Empty and 100 + n = templates_[n]; the Type combo 1 = Generic, 2 = Mackie Control; the output combo is
// 1-based into outputs_ (JUCE reserves id 0 for "nothing selected").

#include "UI/MidiRemote/AddController/AddControllerPopover.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kStartDetectId = 1;
constexpr int kStartEmptyId = 2;
constexpr int kStartTemplateBaseId = 100;
constexpr int kVirtualDeviceId = 1000; // "No device": above every 1-based device index
constexpr int kTypeGenericId = 1;
constexpr int kTypeMackieId = 2;
} // namespace

AddControllerPopover::AddControllerPopover(std::vector<DeviceRow> devices,
                                           std::vector<synth::midi::TemplateInfo> templates,
                                           std::vector<DeviceRow> outputs)
    : devices_(std::move(devices))
    , templates_(std::move(templates))
    , outputs_(std::move(outputs)) {
    typeCombo_.setComponentID("addControllerTypeCombo");
    outputCombo_.setComponentID("addControllerOutputCombo");
    reasonLabel_.setComponentID("addControllerReason");
    deviceCombo_.setComponentID("addControllerDeviceCombo");
    nameEditor_.setComponentID("addControllerNameEditor");
    startCombo_.setComponentID("addControllerStartCombo");
    okButton_.setComponentID("addControllerOk");
    cancelButton_.setComponentID("addControllerCancel");

    int firstFree = 0;
    for (std::size_t i = 0; i < devices_.size(); ++i) {
        const int id = static_cast<int>(i) + 1;
        const auto& row = devices_[i];
        deviceCombo_.addItem(
            row.existingProfileName.isEmpty() ? row.name : row.name + " (" + row.existingProfileName + ")", id);
        if (!row.existingProfileName.isEmpty())
            deviceCombo_.setItemEnabled(id, false);
        else if (firstFree == 0)
            firstFree = id;
    }
    if (!devices_.empty())
        deviceCombo_.addSeparator();
    deviceCombo_.addItem("No device (virtual controller)", kVirtualDeviceId);
    deviceCombo_.setTextWhenNothingSelected("Choose a device");
    deviceCombo_.onChange = [this] { selectDevice(deviceCombo_.getSelectedId()); };

    startCombo_.addItem("Detect controls now", kStartDetectId);
    for (std::size_t i = 0; i < templates_.size(); ++i)
        startCombo_.addItem("Template: " + templates_[i].name, kStartTemplateBaseId + static_cast<int>(i));
    startCombo_.addItem("Empty", kStartEmptyId);
    startCombo_.setSelectedId(kStartDetectId, juce::dontSendNotification);
    startCombo_.onChange = [this] { handleStartChanged(); };

    nameEditor_.onTextChange = [this] {
        nameEditedByUser_ = true;
        refreshOk();
    };

    typeCombo_.addItem("Generic (learn)", kTypeGenericId);
    typeCombo_.addItem("Mackie Control", kTypeMackieId);
    typeCombo_.setSelectedId(kTypeGenericId, juce::dontSendNotification);
    typeCombo_.setTitle("Controller type");
    typeCombo_.setTooltip("Generic learns each control; Mackie Control drives the mixer with motor faders");
    typeCombo_.onChange = [this] { handleTypeChanged(); };
    for (std::size_t i = 0; i < outputs_.size(); ++i)
        outputCombo_.addItem(outputs_[i].name, static_cast<int>(i) + 1);
    outputCombo_.setTextWhenNothingSelected("Choose an output");
    outputCombo_.setTextWhenNoChoicesAvailable("No MIDI outputs");
    outputCombo_.setTitle("MIDI output device");
    outputCombo_.setTooltip("Where the motor faders and button lights are sent");
    outputCombo_.onChange = [this] { refreshOk(); };
    reasonLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    reasonLabel_.setMinimumHorizontalScale(0.8f);

    okButton_.onClick = [this] { confirm(); };
    cancelButton_.onClick = [this] {
        if (onCancelled)
            onCancelled();
    };

    for (juce::Component* c :
         std::initializer_list<juce::Component*>{&typeLabel_, &typeCombo_, &deviceLabel_, &deviceCombo_, &nameLabel_,
                                                 &nameEditor_, &startLabel_, &startCombo_, &okButton_, &cancelButton_})
        addAndMakeVisible(c);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&outputLabel_, &outputCombo_, &reasonLabel_})
        addChildComponent(c);

    // With a free device it is preselected (and prefills the name); with none, the virtual choice is,
    // so a surface can be designed before any hardware is plugged in.
    deviceCombo_.setSelectedId(firstFree != 0 ? firstFree : kVirtualDeviceId, juce::sendNotificationSync);
    refreshOk();
    setSize(kWidth, kHeight);
}

AddControllerPopover::~AddControllerPopover() = default;

// A device change re-prefills the name -- unless the user has typed their own, which a later
// device change must not clobber.
void AddControllerPopover::selectDevice(int comboId) {
    if (comboId == kVirtualDeviceId && !nameEditedByUser_)
        nameEditor_.setText("Virtual controller", juce::dontSendNotification);
    // Detect listens to a device, so a virtual controller cannot start with it.
    startCombo_.setItemEnabled(kStartDetectId, comboId != kVirtualDeviceId);
    if (comboId == kVirtualDeviceId && startCombo_.getSelectedId() == kStartDetectId)
        startCombo_.setSelectedId(kStartEmptyId, juce::dontSendNotification);
    if (comboId >= 1 && comboId <= static_cast<int>(devices_.size()) && !nameEditedByUser_) {
        nameEditor_.setText(devices_[static_cast<std::size_t>(comboId - 1)].name, juce::dontSendNotification);
    }
    refreshOk();
}

// Choosing a template whose handshake declares a port hint (TemplateInfo::handshakePort, e.g. the
// Launch Control XL 3's "DAW") preselects the first free device row whose name already contains
// that word -- e.g. "LCXL3 1 DAW Out" over "LCXL3 1 MIDI Out" -- through the SAME path a manual
// device pick uses (selectDevice()), so the name field also re-prefills (see
// docs/control/midi-remote-device-handshake.md#device-handshake).
void AddControllerPopover::handleStartChanged() {
    const int startId = startCombo_.getSelectedId();
    if (startId >= kStartTemplateBaseId) {
        const auto& hint = templates_[static_cast<std::size_t>(startId - kStartTemplateBaseId)].handshakePort;
        if (hint.isNotEmpty()) {
            const auto it = std::find_if(devices_.begin(), devices_.end(), [&](const DeviceRow& d) {
                return d.existingProfileName.isEmpty() && d.name.contains(hint);
            });
            if (it != devices_.end())
                deviceCombo_.setSelectedId(static_cast<int>(std::distance(devices_.begin(), it)) + 1,
                                           juce::sendNotificationSync);
        }
    }
    refreshOk();
}

bool AddControllerPopover::isMackie() const { return typeCombo_.getSelectedId() == kTypeMackieId; }

// A Mackie Control's layout is the protocol's, so Start with makes no sense for it; its row is taken
// by the output the motors and lights are sent to, which it cannot work without.
void AddControllerPopover::handleTypeChanged() {
    const bool mackie = isMackie();
    startLabel_.setVisible(!mackie);
    startCombo_.setVisible(!mackie);
    outputLabel_.setVisible(mackie);
    outputCombo_.setVisible(mackie);
    deviceCombo_.setItemEnabled(kVirtualDeviceId, !mackie);
    if (mackie && !nameEditedByUser_)
        nameEditor_.setText("Mackie Control", juce::dontSendNotification);
    else if (!mackie && !nameEditedByUser_)
        selectDevice(deviceCombo_.getSelectedId()); // back to the device's own name
    refreshOk();
}

void AddControllerPopover::refreshOk() {
    const auto reason = getDisabledReason();
    okButton_.setEnabled(reason.isEmpty());
    okButton_.setTooltip(reason);
    reasonLabel_.setText(reason, juce::dontSendNotification);
    reasonLabel_.setVisible(reason.isNotEmpty() && isMackie());
}

bool AddControllerPopover::canConfirm() const { return getDisabledReason().isEmpty(); }

juce::String AddControllerPopover::getDisabledReason() const {
    if (nameEditor_.getText().trim().isEmpty())
        return "Give the controller a name";
    const int id = deviceCombo_.getSelectedId();
    if (id == kVirtualDeviceId)
        return isMackie() ? "A Mackie Control needs its MIDI input device" : juce::String();
    if (id < 1 || id > static_cast<int>(devices_.size()))
        return "Choose a MIDI input device";
    if (!devices_[static_cast<std::size_t>(id - 1)].existingProfileName.isEmpty())
        return "That device already has a controller";
    const int outputId = outputCombo_.getSelectedId();
    if (isMackie() && (outputId < 1 || outputId > static_cast<int>(outputs_.size())))
        return "Choose the MIDI output so the faders can move";
    return {};
}

AddControllerPopover::Choice AddControllerPopover::getChoice() const {
    Choice choice;
    const int id = deviceCombo_.getSelectedId();
    if (id >= 1 && id <= static_cast<int>(devices_.size())) {
        choice.deviceIdentifier = devices_[static_cast<std::size_t>(id - 1)].identifier;
        choice.deviceName = devices_[static_cast<std::size_t>(id - 1)].name;
    }
    choice.profileName = nameEditor_.getText().trim();
    if (isMackie()) {
        choice.protocol = synth::ControllerProtocol::mcu;
        choice.startWith = StartWith::empty;
        if (const int outputId = outputCombo_.getSelectedId(); outputId >= 1 && outputId <= (int)outputs_.size()) {
            choice.outputIdentifier = outputs_[static_cast<std::size_t>(outputId - 1)].identifier;
            choice.outputName = outputs_[static_cast<std::size_t>(outputId - 1)].name;
        }
        return choice;
    }

    const int startId = startCombo_.getSelectedId();
    if (startId == kStartEmptyId) {
        choice.startWith = StartWith::empty;
    } else if (startId >= kStartTemplateBaseId) {
        choice.startWith = StartWith::templateLayout;
        choice.templateId = templates_[static_cast<std::size_t>(startId - kStartTemplateBaseId)].id;
    } else if (id != kVirtualDeviceId) {
        choice.startWith = StartWith::detect;
    } else {
        choice.startWith = StartWith::empty;
    }
    return choice;
}

void AddControllerPopover::confirm() {
    if (canConfirm() && onConfirmed)
        onConfirmed(getChoice());
}

void AddControllerPopover::resized() {
    auto bounds = getLocalBounds().reduced(12);
    const auto row = [&](juce::Label& label, juce::Component& field) {
        label.setBounds(bounds.removeFromTop(16));
        field.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(6);
    };
    row(typeLabel_, typeCombo_);
    row(deviceLabel_, deviceCombo_);
    row(nameLabel_, nameEditor_);
    const auto startRow = bounds;
    row(startLabel_, startCombo_);
    bounds = startRow;
    row(outputLabel_, outputCombo_); // the same slot: only one of the two rows shows
    reasonLabel_.setBounds(bounds.removeFromTop(16));
    auto buttons = bounds.removeFromBottom(26);
    cancelButton_.setBounds(buttons.removeFromRight(80));
    buttons.removeFromRight(8);
    okButton_.setBounds(buttons.removeFromRight(80));
}

void AddControllerPopover::paint(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    g.fillAll(lf != nullptr ? lf->getTheme().colors.surface : juce::Colours::black);
}

} // namespace synth::ui
