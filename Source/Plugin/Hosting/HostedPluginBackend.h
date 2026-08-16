#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

namespace synth {

/**
 * The serialized identity of a third-party plugin (TL7-2).
 *
 * Format + uniqueId + name, and DELIBERATELY never a path. A patch that named
 * `/Users/someone/Library/Audio/Plug-Ins/VST3/Foo.vst3` would (a) leak the author's disk layout into
 * a file meant to be shared, (b) not survive moving the plugin or opening the patch on another
 * machine, and (c) hand anything that can write a patch a lever on which file the host opens. The
 * path lives only in the scan list, which is local, rebuilt by scanning, and never serialized into a
 * patch. TL7-3 formalizes that list and the migration of an identity that no longer resolves.
 *
 * Matching is uid-first: `uniqueId` is stable across machines for both VST3 (derived from the FUID)
 * and AU (the component subtype). `name` is only a fallback for plugins whose uid is 0, and is
 * always carried so an unresolved identity can still be SHOWN to the user ("Foo (VST3) not
 * installed") rather than rendered as an opaque number.
 */
struct PluginIdentity {
    juce::String format; ///< juce::PluginDescription::pluginFormatName, e.g. "VST3" / "AudioUnit".
    juce::String name;   ///< Display name; the fallback match key and the only user-legible field.
    int uid = 0;         ///< juce::PluginDescription::uniqueId. 0 means "unknown", match by name.

    bool isValid() const noexcept { return format.isNotEmpty() && (uid != 0 || name.isNotEmpty()); }

    bool operator==(const PluginIdentity& other) const noexcept {
        return format == other.format && name == other.name && uid == other.uid;
    }
    bool operator!=(const PluginIdentity& other) const noexcept { return !(*this == other); }

    /** True when `description` is the plugin this identity names: same format, then uid if we have
     *  one, else name. Never consults fileOrIdentifier. */
    bool matches(const juce::PluginDescription& description) const noexcept;

    static PluginIdentity fromDescription(const juce::PluginDescription& description);

    /** The three JSON properties a node's "state" object carries. Contains no path, by construction
     *  — HostedPluginTests asserts that over the serialized patch. */
    juce::var toVar() const;
    static PluginIdentity fromVar(const juce::var& state);
};

/**
 * The seam between "a module wants a plugin instance" and "a plugin format loads a binary".
 *
 * Two jobs:
 *
 * 1. **Format-agnosticism.** HostedPluginModule never mentions VST3 or AudioUnit; adding CLAP later
 *    (deferred in TL7-1) is a change to the default backend, not to the module.
 *
 * 2. **Testability without third-party binaries.** There is no plugin we can check into this repo
 *    and load in CI, so every behaviour worth pinning — publish, refusal, state round-trip, the
 *    retained-instance discipline — is exercised against a stub backend that hands back an ordinary
 *    juce::AudioPluginInstance subclass. See Tests/StubPluginInstance.h.
 *
 * Threading: `createInstanceAsync` is called on the message thread and its callback fires on the
 * message thread, LATER (never re-entrantly from inside the call). That is JUCE's own
 * AudioPluginFormat::createPluginInstanceAsync contract and the stub matches it via
 * MessageManager::callAsync, so tests exercise the same ordering the real formats produce.
 */
class HostedPluginBackend {
public:
    virtual ~HostedPluginBackend();

    /** Exactly juce::AudioPluginFormat::PluginCreationCallback: an instance on success, or nullptr
     *  plus a human-readable reason. Never both. */
    using InstanceCallback = std::function<void(std::unique_ptr<juce::AudioPluginInstance>, const juce::String& error)>;

    /** Serialized-identity entry point (what a loaded patch has). Resolves through
     *  resolveIdentity() and delegates; an identity that does not resolve fails asynchronously with
     *  a message, which is what leaves the module a valid placeholder. */
    virtual void createInstanceAsync(const PluginIdentity& identity, double sampleRate, int blockSize,
                                     InstanceCallback callback);

    /** Full-description entry point (what a scan list / a file-chooser has). The one subclasses
     *  implement.
     *  NOTE for subclasses: overriding this hides the PluginIdentity overload — re-expose it with
     *  `using HostedPluginBackend::createInstanceAsync;`. */
    virtual void createInstanceAsync(const juce::PluginDescription& description, double sampleRate, int blockSize,
                                     InstanceCallback callback) = 0;

    /** Identity -> description. Base returns false ("nothing known"); the default backend consults
     *  its known-plugins list, which TL7-3's scanner fills. */
    virtual bool resolveIdentity(const PluginIdentity& identity, juce::PluginDescription& out) const;

    /** The process-wide backend. Real formats unless a ScopedDefault override is installed.
     *
     *  This is the seam HostedPluginModule::setExtraState needs: state restore happens deep inside
     *  applyJSONToGraph, which has no business plumbing a backend through, so the module reaches for
     *  the default. Tests swap the default for the duration of a scope rather than passing one in. */
    static HostedPluginBackend& getDefault();

    /** RAII override of getDefault(). Message thread; not nestable across threads. Restores the
     *  previous backend (usually "none", i.e. the real one) on destruction, so a test that throws
     *  cannot leak a dangling stub into the next test. */
    class ScopedDefault {
    public:
        explicit ScopedDefault(HostedPluginBackend* backend);
        ~ScopedDefault();

        ScopedDefault(const ScopedDefault&) = delete;
        ScopedDefault& operator=(const ScopedDefault&) = delete;

    private:
        HostedPluginBackend* previous_ = nullptr;
    };

protected:
    /** Deliver `error` on the message thread, later — the failure path has to keep the same "never
     *  re-entrant" promise as the success path, or a caller that sets state after the call would
     *  overwrite what the callback just wrote. */
    static void failAsync(InstanceCallback callback, const juce::String& error);
};

/**
 * The real backend: VST3 everywhere, AudioUnit additionally on macOS (TL7-1's licensing call —
 * JUCE's built-in hosting only, no new dependency pins, CLAP deferred).
 *
 * The formats are added explicitly rather than via addDefaultFormats() so the set of things this
 * app will load is a decision in source, not a by-product of which JUCE_PLUGINHOST_* flags happen to
 * be set. (Both are in fact set, in the root CMakeLists on Core.)
 *
 * TL7-2 knows no plugins: `knownPlugins_` starts empty, so resolving a serialized identity fails
 * with "not installed" and the module stays a placeholder. TL7-3 fills the list from a real scan.
 */
class DefaultHostedPluginBackend : public HostedPluginBackend {
public:
    DefaultHostedPluginBackend();
    ~DefaultHostedPluginBackend() override;

    using HostedPluginBackend::createInstanceAsync;

    void createInstanceAsync(const juce::PluginDescription& description, double sampleRate, int blockSize,
                             InstanceCallback callback) override;

    bool resolveIdentity(const PluginIdentity& identity, juce::PluginDescription& out) const override;

    /** Message thread. The descriptions a scan produced; replaces whatever was there (TL7-3). */
    void setKnownPlugins(std::vector<juce::PluginDescription> plugins);
    const std::vector<juce::PluginDescription>& getKnownPlugins() const noexcept { return knownPlugins_; }

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager_; }

private:
    juce::AudioPluginFormatManager formatManager_;
    std::vector<juce::PluginDescription> knownPlugins_;
};

} // namespace synth
