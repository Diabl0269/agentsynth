// Concern: the automation lane header's control chips -- what each chip does (the same actions as the lane menu, plus
// the record-mode menu and Mute), whether it can act now, and what its tooltip adds.
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"

namespace synth::ui {

namespace {
const char* const kModeNames[] = {"Off", "Read", "Touch", "Latch", "Write"};
constexpr int kMoveMenuNoTargetId = 1000; // a disabled placeholder item, never applied
} // namespace

LaneControlStrip::Actions AutomationLaneHeaderComponent::controlActions() {
    LaneControlStrip::Actions actions;
    actions.perform = [this](LaneControl control, juce::Component& anchor) { performControl(control, anchor); };
    actions.isEnabled = [this](LaneControl control) { return controlEnabled(control); };
    actions.detail = [this](LaneControl control) { return controlDetail(control); };
    actions.isLit = [this](LaneControl control) {
        const auto* lane = doc_.getLane(laneId_);
        return control == LaneControl::Mute && lane != nullptr && lane->recordMode == (int)synth::LaneRecordMode::Off;
    };
    actions.iconColour = [this](LaneControl control) -> std::optional<juce::Colour> {
        const auto* lane = doc_.getLane(laneId_);
        // Write overwrites the lane while the transport records, so its chip reads as a standing warning.
        if (control == LaneControl::ReadWrite && lane != nullptr &&
            lane->recordMode == (int)synth::LaneRecordMode::Write)
            return synth::theme::themeOf(*this).colors.error;
        return std::nullopt;
    };
    return actions;
}

// Every chip is an existing menu action; applyMenuChoice may destroy this header (Remove), so it is the last call.
void AutomationLaneHeaderComponent::performControl(LaneControl control, juce::Component& anchor) {
    switch (control) {
    case LaneControl::ReadWrite:
        showRecordModeMenu(anchor);
        break;
    case LaneControl::AddMod:
        applyMenuChoice(kAddModulatorMenuId);
        break;
    case LaneControl::ChangeParameter:
        applyMenuChoice(kChangeParameterMenuId);
        break;
    case LaneControl::Mute:
        toggleMute();
        break;
    case LaneControl::Duplicate:
        applyMenuChoice(kDuplicateMenuId);
        break;
    case LaneControl::MoveToTrack:
        showMoveMenu(anchor);
        break;
    case LaneControl::CreateCustomLfo:
        applyMenuChoice(kCreateCustomLfoMenuId);
        break;
    case LaneControl::Remove:
        applyMenuChoice(kDeleteLaneMenuId);
        break;
    case LaneControl::ChangeSource:
    case LaneControl::ShowInCanvas:
        break; // mod lane controls
    }
}

// The lane menu's Create custom LFO item, as the menu would list it (its text says why it is disabled).
juce::PopupMenu::Item AutomationLaneHeaderComponent::createCustomLfoItem() const {
    juce::PopupMenu menu;
    addCreateCustomLfoItem(menu);
    juce::PopupMenu::MenuItemIterator it(menu);
    it.next();
    return it.getItem();
}

bool AutomationLaneHeaderComponent::controlEnabled(LaneControl control) const {
    const auto* lane = doc_.getLane(laneId_);
    if (lane == nullptr)
        return false;
    switch (control) {
    case LaneControl::AddMod:
        return host_ != nullptr && host_->canModulate(lane->nodeUuid, lane->paramId);
    case LaneControl::ChangeParameter:
    case LaneControl::Duplicate:
        return canPickParameter();
    case LaneControl::MoveToTrack:
        return !laneMoveTargets(doc_, laneId_).empty();
    case LaneControl::CreateCustomLfo:
        return createCustomLfoItem().isEnabled;
    default:
        return true;
    }
}

// The tooltip's tail: the current record mode, or why a disabled chip cannot act (a tooltip is all a chip has).
juce::String AutomationLaneHeaderComponent::controlDetail(LaneControl control) const {
    const auto* lane = doc_.getLane(laneId_);
    if (lane == nullptr)
        return {};
    if (control == LaneControl::ReadWrite)
        return juce::String(" (") + kModeNames[juce::jlimit(0, 4, lane->recordMode)] + ")";
    if (controlEnabled(control))
        return {};
    switch (control) {
    case LaneControl::AddMod:
        return " (no CV input)";
    case LaneControl::ChangeParameter:
    case LaneControl::Duplicate:
        return " (no free parameter)";
    case LaneControl::MoveToTrack:
        return " (no other track)";
    case LaneControl::CreateCustomLfo: {
        const auto text = createCustomLfoItem().text;
        const auto base = laneControlName(LaneControl::CreateCustomLfo, LaneControlKind::Automation);
        return text.startsWith(base) ? text.substring(base.length()) : juce::String();
    }
    default:
        return {};
    }
}

// Shows `menu` beside the chip (or hands it to the test hook); the pick is applied after the menu closes.
void AutomationLaneHeaderComponent::showChipMenu(juce::PopupMenu menu, juce::Component& anchor) {
    const auto options = juce::PopupMenu::Options().withTargetComponent(&anchor);
    if (auto& hook = test_hooks::laneMenuHookForTest()) {
        hook(menu, options);
        return;
    }
    juce::Component::SafePointer<AutomationLaneHeaderComponent> safeThis(this);
    menu.showMenuAsync(options, [safeThis](int result) {
        if (auto* self = safeThis.getComponent(); self != nullptr && result != 0)
            self->applyMenuChoice(result);
    });
}

// The five modes in the record-mode combo's ids (mode + 1), the current one ticked.
void AutomationLaneHeaderComponent::showRecordModeMenu(juce::Component& anchor) {
    const auto* lane = doc_.getLane(laneId_);
    juce::PopupMenu menu;
    for (int mode = 0; mode < 5; ++mode)
        menu.addItem(kRecordModeMenuIdBase + mode, kModeNames[mode], true, lane != nullptr && lane->recordMode == mode);
    showChipMenu(std::move(menu), anchor);
}

// The lane menu's own "Move to track" list, targets captured as it opens.
void AutomationLaneHeaderComponent::showMoveMenu(juce::Component& anchor) {
    moveTargets_ = laneMoveTargets(doc_, laneId_);
    juce::PopupMenu menu;
    for (int i = 0; i < (int)moveTargets_.size(); ++i)
        if (const auto* track = doc_.getTrack(moveTargets_[(size_t)i]))
            menu.addItem(kMoveToTrackMenuIdBase + i, track->name);
    if (moveTargets_.empty())
        menu.addItem(kMoveMenuNoTargetId, "No other track", false);
    showChipMenu(std::move(menu), anchor);
}

// Mute is the record mode Off; un-muting puts back the mode it replaced (Read when there is none to put back, for a
// lane that was set to Off by hand).
void AutomationLaneHeaderComponent::toggleMute() {
    const auto* lane = doc_.getLane(laneId_);
    if (lane == nullptr)
        return;
    if (lane->recordMode != (int)synth::LaneRecordMode::Off) {
        modeBeforeMute_ = lane->recordMode;
        applyRecordModeChoice((int)synth::LaneRecordMode::Off + 1);
        return;
    }
    const int back = modeBeforeMute_ > 0 ? modeBeforeMute_ : (int)synth::LaneRecordMode::Read;
    applyRecordModeChoice(back + 1);
}

} // namespace synth::ui
