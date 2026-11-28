// ModRingFades.cpp -- the per-channel fade of a card's modulation rings, on the shared FadeAmount (160 ms in, 110 ms
// out, 80 ms under Reduce Motion, none under Animations Off). docs/layout/mod-ring-fade.md.
#include "ModRingFades.h"
#include "UI/Graph/ModuleComponent/ModChipFade.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
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

ModRingFades::~ModRingFades() {
    for (auto& [channel, entry] : entries_)
        entry.releaseKnob();
}

// The knob joins the card's live layer: the card's picture is re-rastered once without it (the knob's repaint), and
// from then on a ring frame draws the ring and the knob's own picture over it, as the card's paint orders them.
void ModRingFades::holdKnob(int destChannel, juce::Component& knob) {
    const auto it = entries_.find(destChannel);
    if (it == entries_.end() || it->second.knob.getComponent() == &knob)
        return;
    it->second.releaseKnob();
    if (dynamic_cast<RingedKnobPicture*>(knob.getCachedComponentImage()) == nullptr)
        knob.setCachedComponentImage(new RingedKnobPicture(knob, owner_));
    it->second.knob = &knob;
    knob.repaint();
}

// The knob goes back into the card's picture, drawn as before the ring.
void ModRingFades::Entry::releaseKnob() {
    if (auto* k = knob.getComponent())
        if (dynamic_cast<RingedKnobPicture*>(k->getCachedComponentImage()) != nullptr) {
            k->setCachedComponentImage(nullptr);
            k->repaint();
        }
    knob = nullptr;
}

RingedKnobPicture::RingedKnobPicture(juce::Component& knob, juce::Component& card)
    : knob_(knob)
    , card_(&card) {}

// Left out of the card's picture only while that picture renders; the live layer and a snapshot draw it.
void RingedKnobPicture::paint(juce::Graphics& g) {
    if (card_ != nullptr)
        if (auto* cardPicture = dynamic_cast<ZoomFrozenCachedImage*>(card_->getCachedComponentImage());
            cardPicture != nullptr && cardPicture->isRenderingPicture())
            return;
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto bounds = knob_.getLocalBounds();
    const auto imageBounds = bounds * scale;
    if (!valid_ || scale != scale_ || image_.getBounds() != imageBounds) {
        image_ = juce::Image(juce::Image::ARGB, juce::jmax(1, imageBounds.getWidth()),
                             juce::jmax(1, imageBounds.getHeight()), true);
        juce::Graphics imG(image_);
        imG.addTransform(juce::AffineTransform::scale(scale));
        knob_.paintEntireComponent(imG, true);
        scale_ = scale;
        valid_ = true;
    }
    g.setColour(juce::Colours::black.withAlpha(knob_.getAlpha()));
    g.drawImageTransformed(image_,
                           juce::AffineTransform::scale((float)bounds.getWidth() / (float)imageBounds.getWidth(),
                                                        (float)bounds.getHeight() / (float)imageBounds.getHeight()),
                           false);
}

bool RingedKnobPicture::invalidateAll() {
    valid_ = false;
    return true;
}

bool RingedKnobPicture::invalidate(const juce::Rectangle<int>&) {
    valid_ = false;
    return true;
}

void RingedKnobPicture::releaseResources() {
    image_ = {};
    valid_ = false;
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
