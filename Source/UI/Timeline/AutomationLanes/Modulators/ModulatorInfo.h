#pragma once

#include "UI/Theme/Theme.h"
#include <algorithm>
#include <juce_graphics/juce_graphics.h>
#include <map>
#include <vector>

namespace synth::ui {

// One modulation routing into an automated parameter's CV jack, as the timeline sees it: the graph
// is never stored in the timeline doc, so a lane's modulator rows are re-derived from these every
// time the graph changes. Every node is named by uuid (a node id does not survive an undo restore).
struct ModulatorInfo {
    juce::String sourceUuid;       // the module driving the jack (an LFO, an envelope, a macro port...)
    juce::String sourceTitle;      // its card title
    int sourceChannel = 0;         // raw output channel of the source
    bool isLfo = false;            // an LFO gets the full row; anything else is read-only
    bool isEnvelope = false;       // an envelope: only the lane header's marker tooltip tells it from other sources
    juce::String attenuverterUuid; // the hidden depth node; empty for a direct cable (no depth control)
    juce::String targetUuid;       // the lane's node
    juce::String paramId;          // the lane's parameter
    int targetChannel = 0;         // raw CV channel on the target
    juce::Colour colour = synth::theme::Theme{}.colors.modWire; // the modulation wire's resolved colour

    /** What identifies the routing across refreshes (values and titles may change, this may not). */
    juce::String key() const {
        return sourceUuid + "/" + juce::String(sourceChannel) + "/" + attenuverterUuid + "/" +
               juce::String(targetChannel);
    }
};

/** The key a routing is stored under in a lane's modulatorOrder: the Attenuverter's uuid when it has one, else key().
 *  An edit that replaces the Attenuverter (Change source, Change parameter) moves the slot to the new uuid itself. */
inline juce::String modulatorOrderKey(const ModulatorInfo& info) {
    return info.attenuverterUuid.isNotEmpty() ? info.attenuverterUuid : info.key();
}

/** `routings` (graph order) sorted by a lane's stored modulatorOrder: the routings whose key is listed come first, in
 *  the stored order, then every routing not listed in graph order. A stale key (no routing) is ignored. */
inline std::vector<ModulatorInfo> sortedByModulatorOrder(std::vector<ModulatorInfo> routings,
                                                         const std::vector<juce::String>& order) {
    std::vector<ModulatorInfo> sorted;
    sorted.reserve(routings.size());
    for (const auto& key : order) {
        const auto it = std::find_if(routings.begin(), routings.end(),
                                     [&key](const ModulatorInfo& info) { return modulatorOrderKey(info) == key; });
        if (it != routings.end()) {
            sorted.push_back(std::move(*it));
            routings.erase(it);
        }
    }
    for (auto& info : routings)
        sorted.push_back(std::move(info));
    return sorted;
}

/** The routings of one lane's rows (`before` -> `after`, row order) that one change replaced in place, old key() -> new
 *  key(): a routing gone from `before` pairs with a newcomer from the same source output, then the rest pair in row
 *  order when exactly as many went as came. Anything unpaired was really removed or added. */
inline std::map<juce::String, juce::String> replacedRoutings(const std::vector<ModulatorInfo>& before,
                                                             const std::vector<ModulatorInfo>& after) {
    const auto has = [](const std::vector<ModulatorInfo>& infos, const juce::String& key) {
        return std::any_of(infos.begin(), infos.end(), [&key](const ModulatorInfo& info) { return info.key() == key; });
    };
    std::vector<const ModulatorInfo*> gone;
    std::vector<const ModulatorInfo*> came;
    for (const auto& info : before)
        if (!has(after, info.key()))
            gone.push_back(&info);
    for (const auto& info : after)
        if (!has(before, info.key()))
            came.push_back(&info);
    std::map<juce::String, juce::String> pairs;
    for (auto g = gone.begin(); g != gone.end();) {
        const auto c = std::find_if(came.begin(), came.end(), [g](const ModulatorInfo* info) {
            return info->sourceUuid == (*g)->sourceUuid && info->sourceChannel == (*g)->sourceChannel;
        });
        if (c == came.end()) {
            ++g;
            continue;
        }
        pairs[(*g)->key()] = (*c)->key();
        came.erase(c);
        g = gone.erase(g);
    }
    if (gone.size() == came.size())
        for (size_t i = 0; i < gone.size(); ++i)
            pairs[gone[i]->key()] = came[i]->key();
    return pairs;
}

// Where a parameter edit from a modulator row is in its gesture: Begin opens the undo step, Change
// moves the live value, End closes the step; Once is all three for a single click or key press.
enum class ParameterEditPhase { Begin, Change, End, Once };

} // namespace synth::ui
