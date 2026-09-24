// AddControllerPopover.cpp -- FRO134: see the header.
//
// Combo ids: devices are 1-based indices into devices_; the Start-with combo uses 1 = Detect,
// 2 = Empty and 100 + n = templates_[n] (JUCE reserves id 0 for "nothing selected").

#include "UI/MidiRemote/AddController/AddControllerPopover.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kStartDetectId = 1;
constexpr int kStartEmptyId = 2;
constexpr int kStartTemplateBaseId = 100;
} // namespace

AddControllerPopover::AddControllerPopover(std::vector<DeviceRow> devices,
                                           std::vector<synth::midi::TemplateInfo> templates)
    : devices_(std::move(devices))
    , templates_(std::move(templates)) {
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
    deviceCombo_.setTextWhenNothingSelected(devices_.empty() ? "No MIDI input devices found" : "Choose a device");
    deviceCombo_.onChange = [this] { selectDevice(deviceCombo_.getSelectedId()); };

    startCombo_.addItem("Detect controls now", kStartDetectId);
    for (std::size_t i = 0; i < templates_.size(); ++i)
        startCombo_.addItem("Template: " + templates_[i].name, kStartTemplateBaseId + static_cast<int>(i));
    startCombo_.addItem("Empty", kStartEmptyId);
    startCombo_.setSelectedId(kStartDetectId, juce::dontSendNotification);

    nameEditor_.onTextChange = [this] {
        nameEditedByUser_ = true;
        okButton_.setEnabled(canConfirm());
    };

    okButton_.onClick = [this] { confirm(); };
    cancelButton_.onClick = [this] {
        if (onCancelled)
            onCancelled();
    };

    for (juce::Component* c :
         std::initializer_list<juce::Component*>{&deviceLabel_, &deviceCombo_, &nameLabel_, &nameEditor_, &startLabel_,
                                                 &startCombo_, &okButton_, &cancelButton_})
        addAndMakeVisible(c);

    if (firstFree != 0)
        deviceCombo_.setSelectedId(firstFree, juce::sendNotificationSync); // prefills the name
    okButton_.setEnabled(canConfirm());
    setSize(kWidth, kHeight);
}

AddControllerPopover::~AddControllerPopover() = default;

// A device change re-prefills the name -- unless the user has typed their own, which a later
// device change must not clobber.
void AddControllerPopover::selectDevice(int comboId) {
    if (comboId >= 1 && comboId <= static_cast<int>(devices_.size()) && !nameEditedByUser_) {
        nameEditor_.setText(devices_[static_cast<std::size_t>(comboId - 1)].name, juce::dontSendNotification);
    }
    okButton_.setEnabled(canConfirm());
}

bool AddControllerPopover::canConfirm() const {
    const int id = deviceCombo_.getSelectedId();
    if (id < 1 || id > static_cast<int>(devices_.size()))
        return false;
    if (!devices_[static_cast<std::size_t>(id - 1)].existingProfileName.isEmpty())
        return false;
    return nameEditor_.getText().trim().isNotEmpty();
}

AddControllerPopover::Choice AddControllerPopover::getChoice() const {
    Choice choice;
    const int id = deviceCombo_.getSelectedId();
    if (id >= 1 && id <= static_cast<int>(devices_.size())) {
        choice.deviceIdentifier = devices_[static_cast<std::size_t>(id - 1)].identifier;
        choice.deviceName = devices_[static_cast<std::size_t>(id - 1)].name;
    }
    choice.profileName = nameEditor_.getText().trim();

    const int startId = startCombo_.getSelectedId();
    if (startId == kStartEmptyId) {
        choice.startWith = StartWith::empty;
    } else if (startId >= kStartTemplateBaseId) {
        choice.startWith = StartWith::templateLayout;
        choice.templateId = templates_[static_cast<std::size_t>(startId - kStartTemplateBaseId)].id;
    } else {
        choice.startWith = StartWith::detect;
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
    row(deviceLabel_, deviceCombo_);
    row(nameLabel_, nameEditor_);
    row(startLabel_, startCombo_);
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
