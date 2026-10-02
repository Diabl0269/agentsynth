// Concern: MixerPanelComponent's colour dot -- which picker a column's dot opens, how it opens, and the shortcut
// text the row bypass buttons' tooltips name.
#include "Mixer/ChannelMacroLookup.h"
#include "MixerPanelComponent.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"

namespace synth::ui {

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
    if (route == ColourRoute::Track && popup != nullptr)
        juce::CallOutBox::launchAsynchronously(std::move(popup), dotScreenBounds, nullptr);
    else if (route == ColourRoute::Macro && graphEditor_ != nullptr)
        graphEditor_->promptRecolourMacro(macroId, dotScreenBounds);
}

// The row bypass buttons' tooltips name the key; the default letter stands in when no ShortcutManager is installed.
juce::String MixerPanelComponent::bypassShortcutText() const {
    if (shortcuts_ == nullptr)
        return "B";
    return ShortcutManager::keyPressToDisplayString(shortcuts_->getBinding("mixerToggleRowBypass"));
}

} // namespace synth::ui
