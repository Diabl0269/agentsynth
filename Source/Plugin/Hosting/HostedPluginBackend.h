#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

namespace synth {

class PluginScanService;

/**
 * The formats this app hosts, added to `manager` in scan order: VST3 everywhere, AudioUnit
 * additionally on macOS (a licensing call — JUCE's built-in hosting only, no new dependency
 * pins, CLAP deferred).
 *
 * One function rather than addDefaultFormats() at three call sites, because *hosting* and *scanning*
 * must agree: a format the scanner enumerates but the backend cannot instantiate produces a library
 * row that always fails to load, and a format the backend can load but the scanner never walks
 * produces a plugin the user can never find. DefaultHostedPluginBackend, PluginScanService's default
 * candidate source, and the `--scan-plugin` child process all come through here.
 */
void addHostedPluginFormats(juce::AudioPluginFormatManager& manager);

/** The names of those formats ("VST3", and "AudioUnit" on macOS) — what scanAsync() takes. */
juce::StringArray hostedPluginFormatNames();

/**
 * The serialized identity of a third-party plugin.
 *
 * Format + uniqueId + name, and DELIBERATELY never a path. A patch that named
 * `/Users/someone/Library/Audio/Plug-Ins/VST3/Foo.vst3` would (a) leak the author's disk layout into
 * a file meant to be shared, (b) not survive moving the plugin or opening the patch on another
 * machine, and (c) hand anything that can write a patch a lever on which file the host opens. The
 * path lives only in the scan list, which is local, rebuilt by scanning, and never serialized into a
 * patch, and which also handles migrating an identity that no longer resolves.
 *
 * Matching is uid-first: `uniqueId` is stable across machines for both VST3 (derived from the FUID)
 * and AU (the component subtype). `name` is only a fallback for plugins whose uid is 0, and is
 * always carried so an unresolved identity can still be SHOWN to the user ("Foo (VST3) not
 * installed") rather than rendered as an opaque number.
 *
 * PluginScanService owns the scan list that turns an identity back into a description (and is
 * therefore the one place a fileOrIdentifier is stored), and documents the uid-then-name
 * resolution precedence PluginIdentity::matches() sketches here.
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

    //==============================================================================
    // Drag-and-drop payload
    //
    // The module library and the canvas talk over juce::DragAndDropContainer, whose payload is a
    // single juce::var — so a plugin row travels as a prefixed string on the SAME channel as module
    // names and snippet payloads, distinguished by its prefix (the SnippetManager pattern). The
    // payload is the identity, never a path: the library row already knows the description, but
    // putting a fileOrIdentifier on a drag channel any component can read would recreate exactly the
    // leak PluginIdentity exists to prevent.
    //==============================================================================

    static constexpr const char* kDragPayloadPrefix = "plugin:";

    /** "plugin:<format>|<uid>|<name>". Name goes last so it may legally contain '|'. */
    juce::String toDragPayload() const;
    static bool isDragPayload(const juce::String& payload);
    /** Inverse of toDragPayload(). Returns an invalid identity for anything else. */
    static PluginIdentity fromDragPayload(const juce::String& payload);
};

/**
 * The seam between "a module wants a plugin instance" and "a plugin format loads a binary".
 *
 * Two jobs:
 *
 * 1. **Format-agnosticism.** HostedPluginModule never mentions VST3 or AudioUnit; adding CLAP later
 *    is a change to the default backend, not to the module.
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
     *  its known-plugins list, which the scanner fills. */
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
 * The real backend: VST3 everywhere, AudioUnit additionally on macOS (a licensing call —
 * JUCE's built-in hosting only, no new dependency pins, CLAP deferred).
 *
 * The formats are added explicitly (addHostedPluginFormats) rather than via addDefaultFormats() so
 * the set of things this app will load is a decision in source, not a by-product of which
 * JUCE_PLUGINHOST_* flags happen to be set. (Both are in fact set, in the root CMakeLists on Core.)
 *
 * A PluginScanService the OWNER installs fills the identity->description direction
 * (MainComponent, on the standalone path only — a plugin build of ourselves never scans, because
 * inside a host the host owns plugin discovery). With no service installed the backend falls back to
 * `knownPlugins_`, which is empty by default: resolving a serialized identity then fails with "not
 * installed" and the module stays a placeholder.
 */
class DefaultHostedPluginBackend : public HostedPluginBackend {
public:
    DefaultHostedPluginBackend();
    ~DefaultHostedPluginBackend() override;

    using HostedPluginBackend::createInstanceAsync;

    void createInstanceAsync(const juce::PluginDescription& description, double sampleRate, int blockSize,
                             InstanceCallback callback) override;

    /** Scan service first (the live list), then the explicitly-set `knownPlugins_` vector. */
    bool resolveIdentity(const PluginIdentity& identity, juce::PluginDescription& out) const override;

    /** Message thread. Owner-installed; nullptr (the default) restores the behaviour of an
     *  empty list. The service is NOT owned — the owner must clear this before destroying it. */
    void setScanService(PluginScanService* service) noexcept { scanService_ = service; }
    PluginScanService* getScanService() const noexcept { return scanService_; }

    /** Message thread. A description list set directly, without a scan — the seam a test or a
     *  file-chooser-driven load uses. Consulted only after the scan service. */
    void setKnownPlugins(std::vector<juce::PluginDescription> plugins);
    const std::vector<juce::PluginDescription>& getKnownPlugins() const noexcept { return knownPlugins_; }

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager_; }

private:
    juce::AudioPluginFormatManager formatManager_;
    std::vector<juce::PluginDescription> knownPlugins_;
    PluginScanService* scanService_ = nullptr;
};

} // namespace synth
