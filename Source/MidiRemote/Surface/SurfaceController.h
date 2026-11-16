#pragma once

// The message-thread half of the protocol surfaces
// (docs/control/midi-remote-mcu-hui.md#3-surfacecontroller-message-thread-half): binds each Mackie Control profile to
// its port through RemoteSinkRouter, drains the surfaces from RemoteEngine's own 60 Hz tick, maps strips onto mixer
// columns 8 at a time and drives the renderers.

#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "MidiRemote/Surface/McuSurface.h"
#include "MidiRemote/Surface/RemoteSinkRouter.h"
#include "MidiRemote/Surface/SurfaceRenderer.h"
#include "Mixer/MixerModel/MixerModel.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

class AppUndoManager;
class AudioEngine;

namespace synth::midi {

class SurfaceController {
public:
    using NodeID = juce::AudioProcessorGraph::NodeID;
    /** The mixer's columns in panel order (buildMixerSnapshot); called on refreshLayout only. */
    using ColumnsProvider = std::function<std::vector<MixerColumn>()>;

    /** Every reference must outlive the controller. `bindsHardware` false (the plugin build) binds nothing. */
    SurfaceController(RemoteEngine& engine, RemoteSinkRouter& router, juce::AudioProcessorGraph& graph,
                      RemoteActionInvoker& invoker, bool bindsHardware);
    ~SurfaceController();

    // ---- Message thread: wiring ----------------------------------------------------------------
    void setColumnsProvider(ColumnsProvider provider) { columnsProvider_ = std::move(provider); }
    /** Nullable; without a sink nothing is sent back. Call before setProfiles. */
    void setFeedbackSink(RemoteFeedbackSink* sink) noexcept { feedbackSink_ = sink; }
    /** Nullable; brackets a Mute press as one undo step. */
    void setUndoManager(AppUndoManager* undo) noexcept { undo_ = undo; }
    /** Fired for a SELECT press with that strip's node. */
    std::function<void(NodeID)> onSelectNode;
    /** Milliseconds, monotonic; defaults to juce::Time::getMillisecondCounterHiRes. */
    void setClock(std::function<double()> clock) { clock_ = std::move(clock); }

    /** Binds every Mackie Control profile with an input and an output; unbinds the rest. */
    void setProfiles(const std::vector<ControllerProfile>& profiles);
    /** From the reconcile funnel after any graph change; never per tick. */
    void refreshLayout();
    /** Unbinds everything and ends open gestures; call before the graph or the feedback sink goes. */
    void detachAll();

    /** The drain tick (RemoteEngine calls it while a surface is bound); tests call it directly. */
    void drain();

    // ---- Diagnostics (tests) -------------------------------------------------------------------
    int boundUnitCount() const noexcept { return static_cast<int>(units_.size()); }
    int bankOffset() const noexcept { return bankOffset_; }
    /** The node strip `strip` (0-7) of the current bank drives; invalid for an empty slot. */
    NodeID nodeForStrip(int strip) const noexcept;
    int activeGestureCount() const noexcept;

private:
    struct Gesture {
        NodeID node;
        bool active = false;
        bool touched = false;
        double lastMoveMs = 0.0;
    };
    struct Unit {
        juce::String profileId;
        juce::String sourceKey;
        bool hasOutput = false;
        ControllerProfile::Input output;
        std::shared_ptr<McuSurface> surface;
        std::unique_ptr<SurfaceRenderer> renderer; // null without an output or a feedback sink
        std::array<Gesture, mcu::kStripsPerUnit> gestures{};
    };

    void publishOwnedSources();
    void handleEvent(Unit& unit, const SurfaceEvent& event);
    void handleFader(Unit& unit, int strip, int value);
    void handleTouch(Unit& unit, int strip, bool touched);
    void handleButton(Unit& unit, int note);
    void moveBank(int delta);
    void endGesture(Gesture& gesture);
    void expireIdleGestures(Unit& unit, double now);
    SurfaceFrame buildFrame() const;
    juce::RangedAudioParameter* stripParameter(NodeID node, const char* paramId) const;
    double now() const { return clock_ ? clock_() : juce::Time::getMillisecondCounterHiRes(); }

    RemoteEngine& engine_;
    RemoteSinkRouter& router_;
    juce::AudioProcessorGraph& graph_;
    RemoteActionInvoker& invoker_;
    const bool bindsHardware_;
    RemoteFeedbackSink* feedbackSink_ = nullptr;
    AppUndoManager* undo_ = nullptr;
    ColumnsProvider columnsProvider_;
    std::function<double()> clock_;

    std::vector<std::unique_ptr<Unit>> units_;
    std::vector<NodeID> slots_; // Strip + Bus columns in panel order
    int bankOffset_ = 0;        // reset each launch, never persisted
    NodeID selectedNode_;
};

/** What MainComponent owns: the router AudioEngine is given as its sink, and the controller over it.
 *  `engine` and `audio` must outlive it; clear AudioEngine's sink before it is destroyed. */
struct ControlSurfaces {
    ControlSurfaces(RemoteEngine& engine, AudioEngine& audio, RemoteActionInvoker& invoker);

    RemoteSinkRouter router;
    SurfaceController controller; // declared after the router it publishes to
};

} // namespace synth::midi
