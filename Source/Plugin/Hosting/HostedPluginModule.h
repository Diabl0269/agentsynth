#pragma once

#include "../../Modules/ModuleBase.h"
#include "HostedPluginBackend.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

namespace synth {

/**
 * Hosted Plugin — a third-party VST3 (or, on macOS, AU) instrument or effect as a graph module.
 *
 * Fixed at kMaxPluginChannels (16) in/out for the module's entire lifetime. JUCE settles a node's
 * bus layout in the ModuleBase constructor, and renegotiating it later would drop every existing
 * graph connection — but a plugin's real channel count isn't known until an async load completes,
 * necessarily after the node is already wired. getVisibleInput/OutputPortCount report the instance's
 * real counts; hidden channels are cleared every block, and the owner drops routings left on a jack
 * that disappeared (GraphEditor::dropRoutingsOnHiddenJacks — an invisible jack cannot be unplugged).
 * An instance wanting MORE than 16 in or out is REFUSED (getStatusMessage()), never truncated.
 *
 * Passes audio through unmodified until an instance is published — loading is async, and a patch
 * naming a plugin this machine doesn't have never publishes one — so a chain never goes silent for
 * one loading link, and bypass/"no instance" are the same code path.
 *
 * Instance lifetime: `activeInstance_` is an atomic pointer the audio thread acquire-loads once per
 * block. Replacing it (load/reload/unload) retires the old instance into `retired_` on the message
 * thread; nothing is ever deleted on the audio thread, and a retired instance is freed only once the
 * audio thread has demonstrably started two later blocks (`blockCounter_`).
 *
 * State/trust: getExtraState() carries the identity (format + uniqueId + name, never a path) plus
 * the plugin's own opaque state blob, base64'd. "Hosted Plugin" is in AIStateMapper's
 * kNonAuthorableModuleTypes, so untrusted apply never calls setExtraState — a plugin state blob is
 * opaque bytes handed to third-party code and must never arrive from a model. A module holding an
 * identity with no instance is a valid state (a patch opened where the plugin isn't installed).
 *
 * `loadPlugin(PluginIdentity)` resolves through HostedPluginBackend::getDefault() and its
 * PluginScanService, which is the only place a plugin PATH ever lives.
 *
 * `onInstanceChanged` fires on every edge of hasInstance(): once with the instance already gone
 * (before it can be reaped) and once with a freshly published instance live — see
 * retireActiveInstance() for why a listener holding an editor needs that ordering. The DESTRUCTOR
 * fires the gone edge too, before it frees the instance — a node delete destroys this module with an
 * editor window still open on it, and the window has to drop its editor first. `onLatencyChanged`
 * / `onInstancePublished` exist because juce::AudioProcessorGraph only re-derives compensation
 * delays on graph.rebuild(); this class detects a latency change (which can arrive from ANY thread,
 * hence the AsyncUpdater hop) but the owner (MainComponent) decides when to actually rebuild.
 *
 * Added via the library's Plugins section (drag/click), not the module catalogue — a bare instance
 * has no identity and no way to become anything.
 */
class HostedPluginModule : public ModuleBase {
public:
    /** Every channel this node can ever carry, in or out. Fixed at construction, for the node's
     *  entire lifetime — see the class comment. An instance wider than this is refused. */
    static constexpr int kMaxPluginChannels = 16;

    HostedPluginModule();
    ~HostedPluginModule() override;

    //==============================================================================
    // Loading (message thread only)
    //==============================================================================

    /** Start an async load of `description`. Returns immediately; the instance is published from the
     *  backend's callback, on the message thread. Supersedes any load already in flight. */
    void loadPlugin(const juce::PluginDescription& description,
                    HostedPluginBackend& backend = HostedPluginBackend::getDefault());

    /** Start an async load of a serialized identity (what a loaded patch has). An identity the
     *  backend cannot resolve leaves the module a placeholder with the identity retained. */
    void loadPlugin(const PluginIdentity& identity, HostedPluginBackend& backend = HostedPluginBackend::getDefault());

    /** Retire the current instance and forget the identity. The module falls back to pass-through. */
    void unloadPlugin();

    /** True once an instance is published and rendering. */
    bool hasInstance() const noexcept { return activeInstance_.load(std::memory_order_acquire) != nullptr; }

    /** True while a load is in flight (no instance yet, but one is coming). */
    bool isLoading() const noexcept { return loading_; }

    /** The identity this module names, whether or not it resolved. Empty when nothing is loaded. */
    const PluginIdentity& getIdentity() const noexcept { return identity_; }

    /** Display name of the loaded (or pending) plugin; empty when the module is bare. */
    juce::String getPluginName() const { return identity_.name; }

    /** Message thread. Why the module is not currently hosting anything — an over-max refusal, a
     *  format error, "not installed" — or empty when there is nothing to say. Polled by the UI. */
    const juce::String& getStatusMessage() const noexcept { return statusMessage_; }

    /** The live juce::AudioPluginInstance backing this module, or nullptr — message-thread
     *  convenience for HostedPluginEditorWindow to build an editor from. This is NOT the
     *  audio thread's path (processBlock has its own acquire-load; see the class comment) and the
     *  returned pointer must not be retained past the next onInstanceChanged callback: that is
     *  exactly the signal that it may be about to be retired. */
    juce::AudioPluginInstance* getActiveInstanceForEditor() const noexcept {
        return activeInstance_.load(std::memory_order_acquire);
    }

    /** Fired on the message thread on every edge of hasInstance() — see the class comment's
     *  "Editor windows and instance-change notification" section for the exact ordering guarantee.
     *  A single slot (not a listener list): only one HostedPluginEditorWindow can ever be open for
     *  a given module (HostedPluginWindowManager enforces one window per node), so nothing else
     *  needs to observe this today. */
    std::function<void()> onInstanceChanged;

    /** Fired on the MESSAGE thread whenever this module's reported latency actually CHANGED
     *  — a runtime change inside the plugin (hopped off whatever thread reported it; see the class
     *  comment) and an unload's drop back to 0. Deliberately not fired for a publish, which
     *  onInstancePublished below already covers, nor from prepareToPlay: the graph prepares its
     *  nodes from inside its own rebuild and re-reads every node's latency immediately afterwards,
     *  so asking the owner to rebuild from there would re-enter a rebuild already in progress to
     *  redo work it was about to do anyway.
     *
     *  Owned by MainComponent (graph.rebuild() + the status bar's round-trip readout), and
     *  deliberately a SEPARATE slot from onInstanceChanged, which HostedPluginEditorWindow owns. */
    std::function<void()> onLatencyChanged;

    /** Fired on the message thread at the very end of publishInstance(), i.e. once per
     *  completed async load, with the new instance already live. Two things need it: a publish
     *  takes the node's latency 0 -> N, so the graph's compensation is stale until it rebuilds; and
     *  a hosted-plugin automation lane cannot resolve until the instance exists.
     *
     *  Owned by MainComponent, separately from onInstanceChanged. */
    std::function<void()> onInstancePublished;

    //==============================================================================
    // Instance parameters — automation-lane resolution seam, message thread only
    //==============================================================================
    //
    // A hosted plugin's OWN parameters live on the inner juce::AudioPluginInstance, never on this
    // module's own getParameters() (that only ever holds "muted" — see the class comment). They are
    // discovered at load and can change SHAPE between plugin versions, which is the whole reason
    // this surface exists rather than a plain findParameterByID(this, ...): see
    // Source/Timeline/AutomationBinding.h for the resolution/orphan rule that consumes it.
    //
    // A note on JUCE's own hierarchy, since it is easy to assume otherwise: a hosted instance's
    // parameters are juce::AudioProcessorParameter, NOT juce::RangedAudioParameter — the type our
    // own modules' parameters use. A format that gives a parameter a persistent string identity
    // (VST3/AU/LV2) does so by implementing juce::HostedAudioProcessorParameter::getParameterID(),
    // a sibling hierarchy with no NormalisableRange. That is why every lookup below returns a plain
    // AudioProcessorParameter*, and why a lane bound to one needs its own RangeSnapshot to define
    // the denormalised-to-0..1 mapping (AutomationApplier does this) rather than
    // RangedAudioParameter::convertTo0to1, which such a parameter does not have.

    /** Exact stable-id match against the LIVE instance's parameters: the first parameter whose
     *  juce::HostedAudioProcessorParameter::getParameterID() equals `paramId`. A parameter that
     *  does not implement HostedAudioProcessorParameter at all, or implements it returning an empty
     *  string (both mean "this plugin format has no persistent id for this parameter"), never
     *  matches here — see findInstanceParameterByIndex for the narrow legacy fallback that exists
     *  for exactly that case. Returns nullptr when there is no live instance, `paramId` is empty, or
     *  nothing matches. */
    juce::AudioProcessorParameter* findInstanceParameter(const juce::String& paramId) const noexcept;

    /** The live instance's parameter at raw index `index` (i.e. getParameters()[index]), or nullptr
     *  when there is no live instance or the index is out of range. Used ONLY to check what a
     *  lane's stored paramIndexHint currently points at — never to bind a lane by index directly;
     *  see AutomationBinding.h's resolveLaneParameter for the one place that decides when an index
     *  match is safe. */
    juce::AudioProcessorParameter* findInstanceParameterByIndex(int index) const noexcept;

    /** The live instance's CURRENT parameter index for `paramId` (via findInstanceParameter), or -1
     *  when it does not resolve. This is what a lane's paramIndexHint is captured FROM at lane
     *  creation time — never re-derived afterwards; the hint is a snapshot of "where this parameter
     *  was when the lane was made", not a live query. */
    int getInstanceParamIndexFallback(const juce::String& paramId) const noexcept;

    /** One entry in the live instance's parameter list, message-thread snapshot for the automation
     *  lane picker's "Add lane..." entries. */
    struct InstanceParameterInfo {
        int index = -1;
        // The stable id from HostedAudioProcessorParameter::getParameterID(), or, when the plugin
        // format has none, a synthetic "legacy:<index>" key — every instance parameter needs SOME
        // non-empty, lane-identity-usable paramId, and a lane created against this key can only ever
        // resolve back through the index-fallback branch (a literal "legacy:<index>" paramID never
        // occurs on a real plugin), which is exactly the legacy behaviour it names.
        juce::String paramId;
        juce::String displayName;
    };

    /** Every live instance parameter. Empty when there is no live instance (still loading, refused,
     *  or the plugin is not installed) — callers treat that exactly like "nothing to offer yet", the
     *  same transient state HostedPluginModule already models elsewhere. */
    std::vector<InstanceParameterInfo> getInstanceParameters() const;

    //==============================================================================
    // AudioProcessor
    //==============================================================================

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    //==============================================================================
    // Port model
    //==============================================================================

    int getVisibleInputPortCount() const override { return visibleInputs_.load(std::memory_order_relaxed); }
    int getVisibleOutputPortCount() const override { return visibleOutputs_.load(std::memory_order_relaxed); }

    juce::String getInputPortLabel(int channelIndex) const override;
    juce::String getOutputPortLabel(int channelIndex) const override;

    LogicalPort mapInputChannel(int rawChannel) const override;
    LogicalPort mapOutputChannel(int rawChannel) const override;

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::HostedPlugin; }

    //==============================================================================
    // Non-parameter state — trusted path ONLY (see the class comment)
    //==============================================================================

    juce::var getExtraState() const override;
    void setExtraState(const juce::var& state) override;

private:
    /** Message thread. The identity half of loadPlugin(), WITHOUT clearing pendingBlob_ — the state
     *  restore in setExtraState() is the only caller allowed to carry a blob into a load. */
    void startIdentityLoad(const PluginIdentity& identity, HostedPluginBackend& backend);

    /** Message thread. Prepares, validates and publishes `instance`, or refuses it with a reason. */
    void publishInstance(std::unique_ptr<juce::AudioPluginInstance> instance);

    /** Message thread. Moves the live instance into `retired_` and clears the port counts. */
    void retireActiveInstance();

    /** Message thread. Frees retired instances the audio thread has provably let go of. */
    void reapRetired();

    /** Message thread. Comes back later to reap what a just-completed retire could not: at retire
     *  time the audio thread has not moved on yet, so the entry is never reapable in that pass. */
    void scheduleReapRetry();

    /** Message thread. prepareToPlay/setPlayConfigDetails the instance to OUR rate and block. */
    void prepareInstance(juce::AudioPluginInstance& instance) const;

    /** Message thread. setLatencySamples(), then onLatencyChanged — but only when the value really
     *  moved, so an owner never rebuilds its graph for a latency that did not change. */
    void setLatencyAndNotify(int newLatency);

    /** Message thread, from the AsyncUpdater below. Re-mirrors the LIVE instance's latency. */
    void handleInstanceLatencyChanged();

    /** Message thread. Stops listening to whatever instance we currently own, and drops any update
     *  it had already queued — the generation guard for a retired instance's in-flight callback. */
    void detachInstanceListener();

    /** The thread hop for latency-change notification: audioProcessorChanged can arrive on ANY
     *  thread, so the only thing it may do is trigger the (allocation-free, coalescing) AsyncUpdater. */
    class InstanceListener final
        : public juce::AudioProcessorListener
        , private juce::AsyncUpdater {
    public:
        explicit InstanceListener(HostedPluginModule& owner)
            : owner_(owner) {}
        ~InstanceListener() override { cancelPendingUpdate(); }

        /** ANY thread, and by far the highest-frequency callback here: the automation applier writes
         *  hosted parameters from the audio thread every block. It must stay empty. */
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}

        /** ANY thread. */
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override {
            if (details.latencyChanged)
                triggerAsyncUpdate();
        }

        /** Message thread. Drops a queued update — see detachInstanceListener(). */
        void cancelQueuedUpdate() noexcept { cancelPendingUpdate(); }

    private:
        void handleAsyncUpdate() override { owner_.handleInstanceLatencyChanged(); }

        HostedPluginModule& owner_;
    };

    struct RetiredInstance {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        std::uint64_t retiredAtBlock = 0;
    };

    // --- Audio-visible state ---------------------------------------------------------------
    //
    // The published instance. Written on the message thread with release (after prepareToPlay), read
    // on the audio thread with acquire, once per block and never cached across blocks. The instance
    // itself is owned by retired_/pendingOwned_, never by this pointer.
    std::atomic<juce::AudioPluginInstance*> activeInstance_{nullptr};

    // Bumped by the audio thread at the end of EVERY processBlock, including the pass-through and
    // bypass paths — reaping has to make progress in a patch whose hosted module is bypassed.
    std::atomic<std::uint64_t> blockCounter_{0};

    // Visible jack counts. Floor of 1 while empty: a bare module still has to show where audio goes
    // in and comes out. Set to the instance's REAL counts at publish (which may legitimately be 0 in
    // for an instrument), reset to 1 on unload.
    std::atomic<int> visibleInputs_{1};
    std::atomic<int> visibleOutputs_{1};

    // --- Message-thread state --------------------------------------------------------------
    std::unique_ptr<juce::AudioPluginInstance> ownedInstance_; // owns whatever activeInstance_ points at
    std::vector<RetiredInstance> retired_;

    // One listener for the module's whole life, moved from instance to instance — so a queued
    // update can never outlive the object that would deliver it.
    InstanceListener instanceListener_{*this};

    PluginIdentity identity_;
    // Last known plugin state, applied when an instance arrives. Set ONLY by a state restore, and
    // cleared by every other load: it belongs to the plugin it was saved from.
    juce::MemoryBlock pendingBlob_;
    juce::String statusMessage_;
    bool loading_ = false;

    // Generation guard: a callback from a superseded load must not clobber a newer one. Compared
    // against the value captured when the load started.
    int loadGeneration_ = 0;

    double currentSampleRate_ = 44100.0;
    int currentBlockSize_ = 512;
    bool prepared_ = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE(HostedPluginModule)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostedPluginModule)
};

} // namespace synth
