// Concern: MixerPanelComponent's colour dot -- which picker a column's dot opens and how it opens -- and the shortcut
// texts the tooltips name (colour dots, pan knobs, side pane eyes, row bypass buttons).
#include "Mixer/ChannelMacroLookup.h"
#include "MixerPanelComponent.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"

namespace synth::ui {

namespace {
// A closing CallOutBox leaves the keyboard with nobody, so the control that opened the picker (the dot after a click or
// Return, the panel after the colour key) takes it back: its ring shows, and its keys (Space to play) work again.
std::function<void()> giveKeyboardBackTo(juce::Component* opener) {
    return [opener = juce::Component::SafePointer<juce::Component>(opener)] {
        juce::MessageManager::callAsync([opener] {
            if (opener != nullptr && opener->isShowing())
                opener->grabKeyboardFocus();
        });
    };
}
} // namespace

// A channel one track feeds is recoloured through that track (the picker its Timeline swatch opens, which also
// recolours the track's channel macro); a bus, a channel two tracks share and an orphan strip have no single track,
// so they recolour the macro their colour comes from. A channel with neither has nothing to recolour.
MixerPanelComponent::ColourRoute MixerPanelComponent::colourRouteFor(const std::vector<synth::TrackId>& feedingTracks,
                                                                     const juce::String& uuid) const {
    if (feedingTracks.size() == 1 && buildTrackColourPicker && doc_ != nullptr &&
        doc_->getTrack(feedingTracks.front()) != nullptr)
        return ColourRoute::Track;
    if (graph_ != nullptr && macros_ != nullptr && synth::nearestChannelMacro(*graph_, *macros_, uuid) != nullptr)
        return ColourRoute::Macro;
    return ColourRoute::None;
}

MixerPanelComponent::ColourRoute MixerPanelComponent::getColourRouteForTest(const juce::String& uuid) const {
    for (const auto& entry : columnEntries_)
        if (entry.kind == ColumnEntry::Kind::Strip && entry.uuid == uuid)
            return colourRouteFor(entry.feedingTracks, uuid);
    return ColourRoute::None;
}

// The picker is built when the dot is activated (never kept), so it always reads the track's colour as it is now. It
// opens in a CallOutBox anchored on the dot; a pick previews live on every surface through the Timeline's colour
// notification and commits as one undo step (TrackColourPicker.h).
void MixerPanelComponent::openColourPicker(const juce::String& uuid, juce::Rectangle<int> dotScreenBounds) {
    const ColumnEntry* found = nullptr;
    for (const auto& entry : columnEntries_)
        if (entry.kind == ColumnEntry::Kind::Strip && entry.uuid == uuid)
            found = &entry;
    if (found == nullptr)
        return;
    const auto route = colourRouteFor(found->feedingTracks, uuid);
    const auto* macro = route == ColourRoute::Macro ? synth::nearestChannelMacro(*graph_, *macros_, uuid) : nullptr;
    const juce::String macroId = macro != nullptr ? macro->id : juce::String();

    std::unique_ptr<ColourPickerPopup> popup;
    if (route == ColourRoute::Track)
        popup = buildTrackColourPicker(found->feedingTracks.front());
    if (showColourPickerHook_) {
        if (route == ColourRoute::Macro && graphEditor_ != nullptr)
            popup = graphEditor_->createMacroColourPickerForTest(macroId);
        showColourPickerHook_(route, std::move(popup), dotScreenBounds);
        return;
    }
    if (route == ColourRoute::Track && popup != nullptr) {
        popup->onClosed = giveKeyboardBackTo(juce::Component::getCurrentlyFocusedComponent());
        juce::CallOutBox::launchAsynchronously(std::move(popup), dotScreenBounds, nullptr);
    } else if (route == ColourRoute::Macro && graphEditor_ != nullptr)
        graphEditor_->promptRecolourMacro(macroId, dotScreenBounds);
}

// The colour key: the focused strip's picker, anchored on its dot, as a click on the dot opens it. A column with
// nothing to recolour (Direct, Master, a channel with no track or macro) lets the key bubble.
bool MixerPanelComponent::openFocusedColour() {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size())
        return false;
    const auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    if (entry.kind != ColumnEntry::Kind::Strip || colourRouteFor(entry.feedingTracks, entry.uuid) == ColourRoute::None)
        return false;
    openColourPicker(entry.uuid, static_cast<MixerColumnComponent*>(entry.component)->getColourDotScreenBounds());
    return true;
}

// The colour dot's tooltip names the colour key; the default letter stands in when no ShortcutManager is installed.
juce::String MixerPanelComponent::colourShortcutText() const {
    if (shortcuts_ == nullptr)
        return "C";
    return ShortcutManager::keyPressToDisplayString(shortcuts_->getBinding("mixerOpenColour"));
}

void MixerPanelComponent::refreshShortcutHints() {
    toolbar_.refresh();
    zonesPane_.setHideKey(
        [this](const juce::KeyPress& key) {
            return matchesAction(key, "mixerToggleChannelHidden",
                                 juce::KeyPress('h', juce::ModifierKeys::noModifiers, 0));
        },
        shortcuts_ == nullptr
            ? juce::String("H")
            : ShortcutManager::keyPressToDisplayString(shortcuts_->getBinding("mixerToggleChannelHidden")));
    for (const auto& entry : columnEntries_) {
        if (entry.kind != ColumnEntry::Kind::Strip)
            continue;
        auto* column = static_cast<MixerColumnComponent*>(entry.component);
        if (const auto state = stripColumnState_.find(column); state != stripColumnState_.end())
            column->setColourEditable(state->second.colourEditable, colourShortcutText());
        column->setPanShortcutText(panShortcutText());
    }
}

// Both pan keys, for the pan knobs' tooltips.
juce::String MixerPanelComponent::panShortcutText() const {
    if (shortcuts_ == nullptr)
        return "Shift+Left / Shift+Right";
    return ShortcutManager::keyPressToDisplayString(shortcuts_->getBinding("mixerPanLeft")) + " / " +
           ShortcutManager::keyPressToDisplayString(shortcuts_->getBinding("mixerPanRight"));
}

// The row bypass buttons' tooltips name the key; the default letter stands in when no ShortcutManager is installed.
juce::String MixerPanelComponent::bypassShortcutText() const {
    if (shortcuts_ == nullptr)
        return "B";
    return ShortcutManager::keyPressToDisplayString(shortcuts_->getBinding("mixerToggleRowBypass"));
}

} // namespace synth::ui
