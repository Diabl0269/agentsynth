// ModRingFades.cpp -- the per-channel fade of a card's modulation rings, on the shared FadeAmount (160 ms in, 110 ms
// out, 80 ms under Reduce Motion, none under Animations Off). docs/layout/mod-ring-fade.md.
#include "ModRingFades.h"

namespace synth::ui {

// A channel is "modulated" while it has a routing that is not bypassed: that is what the ring shows. The fade is
// per channel, so a second routing on a ringed knob keeps the ring where it is. The first sync snaps, so a project
// that opens already modulated is not a card full of fading rings.
void ModRingFades::sync(const std::vector<ModulationDisplayInfo>& infos, juce::AudioProcessorGraph::NodeID node) {
    std::map<int, std::vector<ModulationDisplayInfo>> live;
    for (const auto& info : infos)
        if (info.destNodeID == node && !info.isBypassed)
            live[info.destChannelIndex].push_back(info);

    const bool snap = !synced_;
    synced_ = true;
    for (auto& [channel, routings] : live) {
        auto& entry = entries_[channel];
        if (entry.fade == nullptr)
            entry.fade = std::make_unique<FadeAmount>(owner_);
        entry.last = std::move(routings);
        if (snap)
            entry.fade->snapTo(true);
        else
            entry.fade->setShown(true);
    }
    for (auto it = entries_.begin(); it != entries_.end();) {
        auto& entry = it->second;
        if (live.count(it->first) == 0) {
            if (snap)
                entry.fade->snapTo(false);
            else
                entry.fade->setShown(false);
        }
        if (!entry.fade->isShown() && entry.fade->value() <= 0.0f && !entry.fade->isFading())
            it = entries_.erase(it);
        else
            ++it;
    }
}

std::vector<ModRingFades::Ring> ModRingFades::rings(const std::vector<ModulationDisplayInfo>& infos,
                                                    juce::AudioProcessorGraph::NodeID node) const {
    std::vector<Ring> out;
    std::map<int, bool> seen;
    for (const auto& info : infos) {
        if (info.destNodeID != node || info.isBypassed)
            continue;
        seen[info.destChannelIndex] = true;
        const auto it = entries_.find(info.destChannelIndex);
        const float alpha = it != entries_.end() ? it->second.fade->value() : (synced_ ? 0.0f : 1.0f);
        if (alpha > 0.0f)
            out.push_back({info, alpha});
    }
    for (const auto& [channel, entry] : entries_)
        if (seen.count(channel) == 0 && entry.fade->value() > 0.0f)
            for (const auto& info : entry.last)
                out.push_back({info, entry.fade->value()});
    return out;
}

float ModRingFades::alphaFor(int destChannel) const {
    const auto it = entries_.find(destChannel);
    return it != entries_.end() ? it->second.fade->value() : 0.0f;
}

} // namespace synth::ui
