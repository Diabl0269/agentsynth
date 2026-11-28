// ModRingFades.cpp -- the per-channel fade of a card's modulation rings, on the shared FadeAmount (160 ms in, 110 ms
// out, 80 ms under Reduce Motion, none under Animations Off). docs/layout/mod-ring-fade.md.
#include "ModRingFades.h"
#include "UI/Graph/ModuleComponent/ModChipFade.h"
#include <cmath>

namespace synth::ui {

ModRingFades::ModRingFades(juce::Component& owner)
    : owner_(owner)
    , chip_(std::make_unique<ModChipFade>(owner)) {}

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
        if (!entry.fade->isShown() && entry.fade->value() <= 0.0f && !entry.fade->isFading()) {
            entry.releaseKnob();
            it = entries_.erase(it);
        } else
            ++it;
    }
}

// What a channel's rings draw is each routing's value, depth band amount and polarity, plus the hover and the fade.
// A value step under 1/2000 of the range is under a pixel on the largest knob, so a source that hardly moves draws the
// same ring. Every routing on a channel is compared with the one it was last drawn as, so two still sources on one knob
// repaint nothing.
const std::vector<ModRingFades::Step>& ModRingFades::steps(const std::vector<ModulationDisplayInfo>& infos,
                                                           juce::AudioProcessorGraph::NodeID node, int hoveredChannel) {
    steps_.clear();
    for (auto& [channel, entry] : entries_) {
        entry.cursor = 0;
        entry.differs = false;
    }
    for (const auto& info : infos) {
        if (info.destNodeID != node || info.isBypassed)
            continue;
        const auto it = entries_.find(info.destChannelIndex);
        if (it == entries_.end())
            continue;
        auto& entry = it->second;
        const bool known = entry.drawn.has_value() && entry.cursor < entry.drawn->routings.size();
        if (!known || std::abs(entry.drawn->routings[entry.cursor].value - info.modSignalValue) >= kRingValueStep ||
            std::abs(entry.drawn->routings[entry.cursor].amount - info.amount) >= 1.0e-5f ||
            entry.drawn->routings[entry.cursor].bipolar != info.sourceBipolar)
            entry.differs = true;
        ++entry.cursor;
    }
    for (auto& [channel, entry] : entries_) {
        const float alpha = entry.fade->value();
        const bool hovered = channel == hoveredChannel;
        if (!entry.drawn.has_value() || entry.cursor != entry.drawn->routings.size() ||
            std::abs(entry.drawn->alpha - alpha) >= 1.0e-4f || entry.drawn->hovered != hovered)
            entry.differs = true;
        if (entry.differs) {
            if (!entry.drawn.has_value())
                entry.drawn.emplace();
            entry.drawn->routings.clear();
            for (const auto& info : infos)
                if (info.destNodeID == node && !info.isBypassed && info.destChannelIndex == channel)
                    entry.drawn->routings.push_back({info.modSignalValue, info.amount, info.sourceBipolar});
            entry.drawn->alpha = alpha;
            entry.drawn->hovered = hovered;
        }
        steps_.push_back({channel, entry.differs});
    }
    return steps_;
}

// A knob keeps its own picture while it shows a ring: a ring step then re-rasters the ring in the card and blits the
// knob instead of painting it again.
void ModRingFades::bufferKnob(int destChannel, juce::Component* knob) {
    const auto it = entries_.find(destChannel);
    if (it == entries_.end() || it->second.knob.getComponent() == knob)
        return;
    it->second.releaseKnob();
    if (knob != nullptr) {
        knob->setBufferedToImage(true);
        it->second.knob = knob;
    }
}

void ModRingFades::Entry::releaseKnob() {
    if (knob != nullptr)
        knob->setBufferedToImage(false);
    knob = nullptr;
}

ModRingFades::~ModRingFades() {
    for (auto& [channel, entry] : entries_)
        entry.releaseKnob();
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
