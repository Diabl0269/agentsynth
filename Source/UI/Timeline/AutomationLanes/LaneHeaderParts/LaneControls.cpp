// Concern: the lane header controls' catalogue (words, order, defaults, per-kind availability) and the shared set of
// shown controls with its persistence. The pictures are in LaneControlIcons.cpp.
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControls.h"

#include "UI/Theme/Theme.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControlStrip.h"
#include <algorithm>

namespace synth::ui {

namespace {
const LaneControlInfo kInfos[] = {
    {LaneControl::ReadWrite, "readWrite", "Read / Write", "Read or write automation"},
    {LaneControl::AddMod, "addMod", "Add mod", "Add a modulation source to this control"},
    {LaneControl::ChangeParameter, "changeParameter", "Change parameter", "Point this lane at another control"},
    {LaneControl::ChangeSource, "changeSource", "Change source", "Point this lane at another modulation source"},
    {LaneControl::Mute, "mute", "Mute", "Mute this lane"},
    {LaneControl::Duplicate, "duplicate", "Duplicate", "Copy this lane below it, with its modulators"},
    {LaneControl::MoveToTrack, "moveToTrack", "Move to track", "Move this lane to another track"},
    {LaneControl::ShowInCanvas, "showInCanvas", "Show in canvas", "Show the module on the canvas"},
    {LaneControl::CreateCustomLfo, "createCustomLfo", "Create custom LFO", "Turn the selected range into an LFO"},
    {LaneControl::Remove, "remove", "Remove lane", "Remove this lane"},
};
} // namespace

const LaneControlInfo& laneControlInfo(LaneControl control) {
    for (const auto& info : kInfos)
        if (info.id == control)
            return info;
    return kInfos[0];
}

juce::String laneControlName(LaneControl control, LaneControlKind kind) {
    if (control == LaneControl::Remove && kind == LaneControlKind::Mod)
        return "Remove modulator";
    return laneControlInfo(control).name;
}

juce::String laneControlTooltip(LaneControl control, LaneControlKind kind) {
    juce::String description = laneControlInfo(control).description;
    if (kind == LaneControlKind::Mod && control == LaneControl::Remove)
        description = "Remove this modulator";
    if (kind == LaneControlKind::Mod && control == LaneControl::ShowInCanvas)
        description = "Show the modulator on the canvas";
    return laneControlName(control, kind) + ": " + description;
}

// There is no non-destructive mute for a modulator's routing (the only writes are the amount and removing it), so a mod
// lane does not offer Mute.
std::vector<LaneControl> laneControlCatalogue(LaneControlKind kind) {
    if (kind == LaneControlKind::Mod)
        return {LaneControl::ChangeSource, LaneControl::ShowInCanvas, LaneControl::Remove};
    return {LaneControl::ReadWrite, LaneControl::AddMod,      LaneControl::ChangeParameter, LaneControl::Mute,
            LaneControl::Duplicate, LaneControl::MoveToTrack, LaneControl::CreateCustomLfo, LaneControl::Remove};
}

std::vector<LaneControl> defaultLaneControls(LaneControlKind kind) {
    if (kind == LaneControlKind::Mod)
        return {LaneControl::ChangeSource};
    return {LaneControl::ReadWrite, LaneControl::AddMod};
}

juce::Colour laneControlColour(LaneControl control, const synth::theme::Theme& theme) {
    switch (control) {
    case LaneControl::ReadWrite:
        return juce::Colour(0xff22d3ee);
    case LaneControl::AddMod:
    case LaneControl::CreateCustomLfo:
        return juce::Colour(0xff9b7bff);
    case LaneControl::ChangeParameter:
        return juce::Colour(0xffff8a3d);
    case LaneControl::Mute:
        return juce::Colour(0xffe0a33d);
    case LaneControl::ShowInCanvas:
        return theme.colors.accent;
    case LaneControl::Remove:
        return theme.colors.error;
    case LaneControl::ChangeSource:
    case LaneControl::Duplicate:
    case LaneControl::MoveToTrack:
        return theme.colors.textPrimary;
    }
    return theme.colors.textPrimary;
}

const char* laneControlSettingsKey(LaneControlKind kind) {
    return kind == LaneControlKind::Mod ? "timelineLaneControls.mod" : "timelineLaneControls.automation";
}

juce::String serialiseLaneControls(const std::vector<LaneControl>& controls) {
    juce::StringArray keys;
    for (const auto control : controls)
        keys.add(laneControlInfo(control).key);
    return keys.joinIntoString(",");
}

std::vector<LaneControl> parseLaneControls(LaneControlKind kind, const juce::String& text) {
    const auto catalogue = laneControlCatalogue(kind);
    std::vector<LaneControl> out;
    for (const auto& token : juce::StringArray::fromTokens(text, ",", "")) {
        for (const auto control : catalogue)
            if (token.trim() == laneControlInfo(control).key && std::find(out.begin(), out.end(), control) == out.end())
                out.push_back(control);
    }
    return out;
}

void LaneControlSet::setProperties(juce::ApplicationProperties* props) {
    props_ = props;
    for (const auto kind : {LaneControlKind::Automation, LaneControlKind::Mod}) {
        auto& entry = slot(kind);
        entry.reset();
        if (props_ != nullptr && props_->getUserSettings() != nullptr &&
            props_->getUserSettings()->containsKey(laneControlSettingsKey(kind)))
            entry = parseLaneControls(kind, props_->getUserSettings()->getValue(laneControlSettingsKey(kind)));
        listeners_.call([kind](Listener& l) { l.laneControlsChanged(kind); });
    }
}

std::vector<LaneControl> LaneControlSet::shown(LaneControlKind kind) const {
    const auto it = sets_.find(kind);
    if (it != sets_.end() && it->second.has_value())
        return *it->second;
    return defaultLaneControls(kind);
}

void LaneControlSet::setShown(LaneControlKind kind, std::vector<LaneControl> controls) {
    if (controls == shown(kind))
        return;
    slot(kind) = controls;
    if (props_ != nullptr && props_->getUserSettings() != nullptr) {
        props_->getUserSettings()->setValue(laneControlSettingsKey(kind), serialiseLaneControls(controls));
        props_->saveIfNeeded();
    }
    listeners_.call([kind](Listener& l) { l.laneControlsChanged(kind); });
}

void LaneControlSet::claimEditing(LaneControlStrip* strip) {
    if (editing_ == strip)
        return;
    auto* previous = editing_;
    editing_ = strip;
    if (previous != nullptr)
        previous->exitEdit();
}

void LaneControlSet::releaseEditing(LaneControlStrip* strip) noexcept {
    if (editing_ == strip)
        editing_ = nullptr;
}

} // namespace synth::ui
