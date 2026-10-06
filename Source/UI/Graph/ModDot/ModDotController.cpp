#include "ModDotController.h"

#include "AudioEngine/AudioEngine.h"
#include "ModDotPopover.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kKeyTooltipMs = 1200;

const KnobModSource* findSource(const std::vector<KnobModSource>& sources, juce::AudioProcessorGraph::NodeID id) {
    for (const auto& s : sources)
        if (s.attenuverterId == id)
            return &s;
    return nullptr;
}

int percentOf(float amount) { return juce::roundToInt(amount * 100.0f); }
} // namespace

ModDotController::ModDotController(GraphEditor& editor, juce::Component& canvas)
    : editor_(editor)
    , tooltip_(canvas) {
    keyHideTimer_.fire = [this] { tooltip_.hide(); };
    // A click on the dot of the panel that is already open closes it.
    onModDotClicked = [this](juce::AudioProcessorGraph::NodeID card, int destChannel, juce::Component& anchor) {
        if (auto* open = getPopover(); open != nullptr && open->card() == card && open->destChannel() == destChannel) {
            closePopover();
            return;
        }
        openPopover(card, destChannel, anchor);
    };
}

juce::AudioProcessorGraph::NodeID ModDotController::chosenAttenuverter(juce::AudioProcessorGraph::NodeID card,
                                                                       int destChannel) const {
    const auto sources = knobModSources(editor_, card, destChannel);
    if (sources.empty())
        return {};
    if (const auto it = lastChosen_.find({card.uid, destChannel}); it != lastChosen_.end())
        if (findSource(sources, juce::AudioProcessorGraph::NodeID(it->second)) != nullptr)
            return juce::AudioProcessorGraph::NodeID(it->second);
    return sources.front().attenuverterId;
}

void ModDotController::setLastChosen(juce::AudioProcessorGraph::NodeID card, int destChannel,
                                     juce::AudioProcessorGraph::NodeID attenuverterId) {
    lastChosen_[{card.uid, destChannel}] = attenuverterId.uid;
}

bool ModDotController::routingsChanged(const std::vector<ModulationRouting>& routings) {
    juce::uint64 hash = 1469598103934665603ull; // FNV-1a over the fields that decide which dots exist
    auto mix = [&hash](juce::uint64 v) { hash = (hash ^ v) * 1099511628211ull; };
    for (const auto& r : routings) {
        mix((juce::uint64)r.kind);
        mix(r.sourceNodeID.uid);
        mix((juce::uint64)r.sourceChannelIndex);
        mix(r.destNodeID.uid);
        mix((juce::uint64)r.destChannelIndex);
        mix(r.attenuverterNodeID.uid);
    }
    mix(routings.size());
    const bool changed = hash != routingSignature_;
    routingSignature_ = hash;
    return changed;
}

bool ModDotController::recountIfRoutingsChanged(const std::vector<ModulationRouting>& routings) {
    if (!routingsChanged(routings))
        return false;
    knobSourceCounts_ = countKnobModSources(editor_, routings);
    return true;
}

int ModDotController::knobSourceCount(juce::AudioProcessorGraph::NodeID card, int destChannel) const {
    const auto it = knobSourceCounts_.find({card.uid, destChannel});
    return it != knobSourceCounts_.end() ? it->second : 0;
}

void ModDotController::setAmount(juce::AudioProcessorGraph::NodeID attenuverter, float target) {
    const float current = attenuverterAmount(editor_.getAudioEngine().getGraph(), attenuverter, target);
    editor_.adjustModAmount(attenuverter, juce::jlimit(-1.0f, 1.0f, target) - current);
}

void ModDotController::pressed(juce::AudioProcessorGraph::NodeID card, int destChannel, juce::Slider& knob,
                               juce::Component& anchor, const juce::MouseEvent& e) {
    keyHideTimer_.stopTimer();
    cancelled_ = false;
    Gesture g;
    g.card = card;
    g.destChannel = destChannel;
    g.attenuverter = chosenAttenuverter(card, destChannel);
    g.knob = &knob;
    g.anchor = &anchor;
    g.startPos = e.position;
    g.startAmount = attenuverterAmount(editor_.getAudioEngine().getGraph(), g.attenuverter);
    g.doubleClick = e.getNumberOfClicks() >= 2 && editor_.getDoubleClickPortDisconnectEnabled();
    gesture_ = g;
    if (g.doubleClick)
        dotDoubleClicked(card, destChannel, anchor);
}

void ModDotController::dragged(const juce::MouseEvent& e) {
    if (!gesture_.has_value() || cancelled_ || gesture_->doubleClick || gesture_->attenuverter.uid == 0)
        return;
    auto& g = *gesture_;
    if (!g.started) {
        if (e.position.getDistanceFrom(g.startPos) < kClickThresholdPx)
            return;
        g.started = true;
        editor_.beginModAmountGesture(); // captured lazily: a click leaves no undo step
    }
    setAmount(g.attenuverter, g.startAmount + (g.startPos.y - e.position.y) * kAmountPerPixel);
    if (g.knob != nullptr)
        showTooltip(g.card, g.destChannel, g.attenuverter, *g.knob);
}

void ModDotController::released(const juce::MouseEvent&) {
    if (!gesture_.has_value())
        return;
    const auto g = *gesture_;
    gesture_.reset();
    if (cancelled_) {
        cancelled_ = false;
        return;
    }
    if (g.doubleClick)
        return;
    if (g.started) {
        editor_.commitModAmountGesture();
        tooltip_.hide();
        return;
    }
    if (onModDotClicked && g.anchor != nullptr)
        onModDotClicked(g.card, g.destChannel, *g.anchor);
}

void ModDotController::cancel() {
    if (!gesture_.has_value())
        return;
    if (gesture_->started)
        setAmount(gesture_->attenuverter, gesture_->startAmount);
    gesture_->started = false;
    cancelled_ = true; // the rest of the press is swallowed; the release resets this
    tooltip_.hide();
}

void ModDotController::step(juce::AudioProcessorGraph::NodeID card, int destChannel, juce::Slider& knob, float delta) {
    const auto attenuverter = chosenAttenuverter(card, destChannel);
    if (attenuverter.uid == 0)
        return;
    const float current = attenuverterAmount(editor_.getAudioEngine().getGraph(), attenuverter);
    editor_.beginModAmountGesture();
    setAmount(attenuverter, current + delta);
    editor_.commitModAmountGesture();
    showTooltip(card, destChannel, attenuverter, knob);
    scheduleKeyHide();
}

void ModDotController::showTooltip(juce::AudioProcessorGraph::NodeID card, int destChannel,
                                   juce::AudioProcessorGraph::NodeID attenuverter, juce::Slider& knob) {
    const auto sources = knobModSources(editor_, card, destChannel);
    const auto* source = findSource(sources, attenuverter);
    const float amount = attenuverterAmount(editor_.getAudioEngine().getGraph(), attenuverter);
    const juce::String name = source != nullptr ? source->sourceName : juce::String();
    tooltip_.show(knob, name, amount);
    announce(name, amount);
}

// One announcement per change of the whole percent, so a slow drag is not a flood of speech.
void ModDotController::announce(const juce::String& name, float amount) {
    const int pct = percentOf(amount);
    if (pct == lastAnnouncedPercent_)
        return;
    lastAnnouncedPercent_ = pct;
    const juce::String signedPct = pct > 0 ? "+" + juce::String(pct) : juce::String(pct);
    juce::AccessibilityHandler::postAnnouncement(name + ", " + signedPct + " percent",
                                                 juce::AccessibilityHandler::AnnouncementPriority::low);
}

void ModDotController::scheduleKeyHide() { keyHideTimer_.startTimer(kKeyTooltipMs); }

} // namespace synth::ui
