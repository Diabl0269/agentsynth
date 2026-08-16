#pragma once

#include "../../Modules/ModuleBase.h"
#include "HostedPluginBackend.h"
#include <atomic>
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

namespace synth {

/**
 * Hosted Plugin — a third-party VST3 (or, on macOS, AU) instrument or effect as a graph module
 * (TL7-2).
 *
 * -- The 16/16 invariant ------------------------------------------------------------------------
 *
 * `ModuleBase("Hosted Plugin", kMaxPluginChannels, kMaxPluginChannels)`, with
 * kMaxPluginChannels = 16, ALWAYS — regardless of the plugin eventually loaded, and regardless of
 * whether one is ever loaded at all.
 *
 * This is forced, not chosen. JUCE settles a node's bus layout in the ModuleBase constructor, and
 * renegotiating it afterwards would drop every graph connection the node already has. A hosted
 * plugin's channel count is not known until an async load completes, which is necessarily AFTER the
 * node exists and is already wired — so the node has to be able to carry the widest plugin we are
 * willing to host from the moment it is created. Same pattern as the Macro bank's knob count and
 * Audio Input's device channels: fixed maximum, varying VISIBLE port count
 * (getVisibleInput/OutputPortCount return the instance's real counts), hidden channels cleared every
 * block, and the owner drops routings left on a jack that disappeared —
 * GraphEditor::dropRoutingsOnHiddenJacks; an invisible jack cannot be unplugged.
 *
 * The other half of the invariant: an instance wanting MORE than 16 in or out is REFUSED rather
 * than truncated. Truncating would silently drop a surround plugin's channels with no way for the
 * user to tell; refusing sets getStatusMessage() and leaves the module passing audio through.
 *
 * -- Pass-through until ready -------------------------------------------------------------------
 *
 * Loading is asynchronous (JUCE's own contract, and a plugin scan/load can take seconds). Until an
 * instance is published — and forever, if the patch names a plugin this machine does not have — the
 * module passes audio through unmodified. A chain does not go silent because one link is still
 * loading, and a patch opened without a plugin installed still plays the rest of itself. That dry
 * path is also what makes the bypass branch trivial: bypass and "no instance" are the same code.
 *
 * -- Instance lifetime: the audio thread never frees ---------------------------------------------
 *
 * The Sampler's retained-instance discipline. `activeInstance_` is a raw atomic pointer the audio
 * thread acquire-loads once per block. Replacing it (a load, a reload, an unload) RETIRES the old
 * instance into `retired_` on the message thread; nothing is ever deleted on the audio thread, and a
 * retired instance is only actually freed once the audio thread has demonstrably started two later
 * blocks (`blockCounter_`), so it cannot still be inside the old processBlock. Publication happens
 * only after prepareToPlay has run on the new instance, so the audio thread never sees an
 * unprepared one.
 *
 * -- State and trust (TL7-4) --------------------------------------------------------------------
 *
 * getExtraState() carries the identity (format + uniqueId + name, never a path) plus the plugin's
 * own opaque state blob, base64'd. It therefore rides the EXISTING trusted-only setExtraState path:
 * "Hosted Plugin" is in AIStateMapper's kNonAuthorableModuleTypes, so validatePatch(trusted=false)
 * rejects the type outright, and untrusted apply never calls setExtraState at all. A plugin state
 * blob is an opaque byte string handed straight to third-party code — the one thing that must never
 * arrive from a model.
 *
 * Restoring is async and may fail: a module holding an identity with no instance is a VALID state,
 * not an error. It is what a patch opened on a machine without the plugin leaves behind, and
 * getExtraState keeps re-serializing the identity and the last known blob so re-saving the patch
 * there does not destroy it.
 *
 * -- Where an identity becomes a plugin (TL7-3) --------------------------------------------------
 *
 * loadPlugin(PluginIdentity) asks HostedPluginBackend::getDefault() to resolve it, and the default
 * backend asks the PluginScanService the app installed on it. So "which binary is this?" is answered
 * exclusively by the scan list — a local, rebuildable index that is the only place a plugin PATH
 * ever lives. See PluginScanService for the resolution precedence and the out-of-process scan.
 *
 * -- Forward pointers ---------------------------------------------------------------------------
 *
 * This module is still absent from the library's module catalogue and from the replace menu: it is
 * added by dragging or clicking a row in the library's Plugins section, which supplies the identity
 * (a bare "Hosted Plugin" hosting nothing would have no way to become anything). TL7-5/6 plugin
 * editor windows and parameter exposure; TL7-7 latency compensation beyond the setLatencySamples()
 * call made here.
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
    // Non-parameter state — trusted path ONLY (see the class comment / TL7-4)
    //==============================================================================

    juce::var getExtraState() const override;
    void setExtraState(const juce::var& state) override;

private:
    /** Message thread. Prepares, validates and publishes `instance`, or refuses it with a reason. */
    void publishInstance(std::unique_ptr<juce::AudioPluginInstance> instance);

    /** Message thread. Moves the live instance into `retired_` and clears the port counts. */
    void retireActiveInstance();

    /** Message thread. Frees retired instances the audio thread has provably let go of. */
    void reapRetired();

    /** Message thread. prepareToPlay/setPlayConfigDetails the instance to OUR rate and block. */
    void prepareInstance(juce::AudioPluginInstance& instance) const;

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

    PluginIdentity identity_;
    juce::MemoryBlock pendingBlob_; // last known plugin state; applied when an instance arrives
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
