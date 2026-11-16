// SurfaceController: binding, the layout, the drain tick and the frame the renderers diff against.
// The per-event mapping (faders, touch, buttons, banking) is SurfaceControllerEvents.cpp.
#include "SurfaceController.h"

#include "AudioEngine/AudioEngine.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Mixer/MixerFaderTaper.h"

#include <algorithm>
#include <cmath>

namespace synth::midi {

SurfaceController::SurfaceController(RemoteEngine& engine, RemoteSinkRouter& router, juce::AudioProcessorGraph& graph,
                                     RemoteActionInvoker& invoker, bool bindsHardware)
    : engine_(engine)
    , router_(router)
    , graph_(graph)
    , invoker_(invoker)
    , bindsHardware_(bindsHardware) {}

SurfaceController::~SurfaceController() { detachAll(); }

// The plugin build binds no surface: it never opens a hardware port.
ControlSurfaces::ControlSurfaces(RemoteEngine& engine, AudioEngine& audio, RemoteActionInvoker& invoker)
    : router(engine)
    , controller(engine, router, audio.getGraph(), invoker, !audio.isHosted()) {}

// One unit per Mackie Control profile on a real port. A unit that survives a republish (same profile,
// same port) keeps its surface, so events already queued on its ring are not lost; its renderer is
// rebuilt only when the output changed. A newly bound unit starts dirty: a full resync on bind.
// The plugin build never binds (it opens no hardware port, Source/CLAUDE.md), so its profile is inert.
void SurfaceController::setProfiles(const std::vector<ControllerProfile>& profiles) {
    std::vector<std::unique_ptr<Unit>> next;
    bool tableChanged = false;
    for (const auto& profile : profiles) {
        if (!bindsHardware_ || profile.protocol != ControllerProtocol::mcu || profile.isVirtual())
            continue;
        auto it = std::find_if(units_.begin(), units_.end(), [&](const auto& unit) {
            return unit != nullptr && unit->profileId == profile.id && unit->sourceKey == profile.input.identifier;
        });
        std::unique_ptr<Unit> unit;
        if (it != units_.end()) {
            unit = std::move(*it);
        } else {
            unit = std::make_unique<Unit>();
            unit->profileId = profile.id;
            unit->sourceKey = profile.input.identifier;
            unit->surface = std::make_shared<McuSurface>();
            tableChanged = true;
        }
        const bool outputChanged = unit->hasOutput != profile.hasOutput ||
                                   unit->output.identifier != profile.output.identifier ||
                                   unit->output.name != profile.output.name;
        if (outputChanged || (unit->renderer == nullptr && profile.hasOutput && feedbackSink_ != nullptr)) {
            unit->hasOutput = profile.hasOutput;
            unit->output = profile.output;
            unit->renderer = profile.hasOutput && feedbackSink_ != nullptr
                                 ? std::make_unique<SurfaceRenderer>(*feedbackSink_, profile.output)
                                 : nullptr;
        }
        next.push_back(std::move(unit));
    }
    for (auto& gone : units_)
        if (gone != nullptr) {
            tableChanged = true;
            for (auto& gesture : gone->gestures)
                endGesture(gesture);
        }
    units_ = std::move(next);
    if (tableChanged)
        publishOwnedSources();
    engine_.setSurfaceDrain(units_.empty() ? std::function<void()>{} : [this] { drain(); });
}

void SurfaceController::publishOwnedSources() {
    std::vector<OwnedSourceTable::Entry> entries;
    for (const auto& unit : units_)
        entries.push_back({unit->sourceKey, unit->surface});
    router_.setOwnedSources(std::move(entries));
}

// Strip and Bus columns are the slots, in the mixer's own order; Direct has no fader and Master is
// not on a strip. A gesture whose node is gone is dropped without endChangeGesture -- its parameter no
// longer exists (the same rule RemoteEngine::reconcile follows).
void SurfaceController::refreshLayout() {
    slots_.clear();
    if (columnsProvider_)
        for (const auto& column : columnsProvider_())
            if (column.kind == MixerColumn::Kind::Strip || column.kind == MixerColumn::Kind::Bus)
                slots_.push_back(column.nodeId);
    for (auto& unit : units_)
        for (auto& gesture : unit->gestures)
            if (gesture.active && graph_.getNodeForId(gesture.node) == nullptr)
                gesture = Gesture{};
    moveBank(0);
}

void SurfaceController::detachAll() {
    for (auto& unit : units_)
        for (auto& gesture : unit->gestures)
            endGesture(gesture);
    const bool hadUnits = !units_.empty();
    units_.clear();
    if (hadUnits) {
        router_.setOwnedSources({});
        engine_.setSurfaceDrain({});
    }
}

void SurfaceController::drain() {
    const double t = now();
    bool anyRenderer = false;
    for (auto& unit : units_) {
        unit->surface->drain([this, &unit](const SurfaceEvent& event) { handleEvent(*unit, event); });
        expireIdleGestures(*unit, t);
        anyRenderer = anyRenderer || unit->renderer != nullptr;
    }
    if (anyRenderer) {
        const auto frame = buildFrame();
        for (auto& unit : units_)
            if (unit->renderer != nullptr)
                unit->renderer->render(frame);
    }
    router_.collectRetired();
}

SurfaceController::NodeID SurfaceController::nodeForStrip(int strip) const noexcept {
    const int slot = bankOffset_ + strip;
    if (strip < 0 || strip >= mcu::kStripsPerUnit || slot >= static_cast<int>(slots_.size()))
        return {};
    return slots_[static_cast<std::size_t>(slot)];
}

int SurfaceController::activeGestureCount() const noexcept {
    int count = 0;
    for (const auto& unit : units_)
        for (const auto& gesture : unit->gestures)
            count += gesture.active ? 1 : 0;
    return count;
}

juce::RangedAudioParameter* SurfaceController::stripParameter(NodeID node, const char* paramId) const {
    auto* graphNode = node == NodeID{} ? nullptr : graph_.getNodeForId(node);
    if (graphNode == nullptr || dynamic_cast<ChannelStripModule*>(graphNode->getProcessor()) == nullptr)
        return nullptr;
    return findParameterByID(graphNode->getProcessor(), paramId);
}

// The surface's picture of the current bank: each strip's gain through the mixer fader's own taper (so
// the motor sits where the on-screen fader does), its mute/solo state and whether it is the strip
// this surface last selected. An empty slot shows a lowered fader and dark buttons.
SurfaceFrame SurfaceController::buildFrame() const {
    SurfaceFrame frame;
    for (int strip = 0; strip < mcu::kStripsPerUnit; ++strip) {
        const auto node = nodeForStrip(strip);
        auto* gain = stripParameter(node, "gain");
        if (gain == nullptr)
            continue;
        const float db = gain->convertFrom0to1(gain->getValue());
        frame.fader[static_cast<std::size_t>(strip)] =
            juce::roundToInt(synth::ui::faderDbToFraction(db) * static_cast<float>(mcu::kFaderMax));
        if (auto* muted = stripParameter(node, "muted"))
            frame.led[static_cast<std::size_t>(mcu::kMuteFirstNote + strip)] = muted->getValue() > 0.5f;
        auto* channel = dynamic_cast<ChannelStripModule*>(graph_.getNodeForId(node)->getProcessor());
        frame.led[static_cast<std::size_t>(mcu::kSoloFirstNote + strip)] = channel->isSoloed();
        frame.led[static_cast<std::size_t>(mcu::kSelectFirstNote + strip)] = node == selectedNode_;
    }
    return frame;
}

} // namespace synth::midi
