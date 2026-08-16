#include "HostedPluginBackend.h"

namespace synth {

//==============================================================================
// PluginIdentity
//==============================================================================

bool PluginIdentity::matches(const juce::PluginDescription& description) const noexcept {
    if (format != description.pluginFormatName)
        return false;
    // uid-first: stable across machines and across a rename. Name is only for uid-less entries.
    if (uid != 0)
        return uid == description.uniqueId;
    return name.isNotEmpty() && name == description.name;
}

PluginIdentity PluginIdentity::fromDescription(const juce::PluginDescription& description) {
    PluginIdentity identity;
    identity.format = description.pluginFormatName;
    identity.name = description.name;
    identity.uid = description.uniqueId;
    return identity;
}

juce::var PluginIdentity::toVar() const {
    juce::DynamicObject::Ptr object = new juce::DynamicObject();
    object->setProperty("pluginFormat", format);
    object->setProperty("pluginName", name);
    object->setProperty("pluginUid", uid);
    return juce::var(object.get());
}

PluginIdentity PluginIdentity::fromVar(const juce::var& state) {
    PluginIdentity identity;
    if (auto* object = state.getDynamicObject()) {
        identity.format = object->getProperty("pluginFormat").toString();
        identity.name = object->getProperty("pluginName").toString();
        identity.uid = static_cast<int>(object->getProperty("pluginUid"));
    }
    return identity;
}

//==============================================================================
// HostedPluginBackend
//==============================================================================

HostedPluginBackend::~HostedPluginBackend() = default;

bool HostedPluginBackend::resolveIdentity(const PluginIdentity&, juce::PluginDescription&) const { return false; }

void HostedPluginBackend::failAsync(InstanceCallback callback, const juce::String& error) {
    if (callback == nullptr)
        return;

    juce::MessageManager::callAsync([callback = std::move(callback), error]() mutable { callback(nullptr, error); });
}

void HostedPluginBackend::createInstanceAsync(const PluginIdentity& identity, double sampleRate, int blockSize,
                                              InstanceCallback callback) {
    if (!identity.isValid()) {
        failAsync(std::move(callback), "No plugin identity.");
        return;
    }

    juce::PluginDescription description;
    if (!resolveIdentity(identity, description)) {
        // Not an error the user can fix by retrying: the plugin is not in the scan list. The module
        // deliberately keeps the identity and stays a placeholder, so re-saving the patch does not
        // silently drop a plugin the author has merely not installed on THIS machine (TL7-3).
        failAsync(std::move(callback), identity.name + " (" + identity.format + ") is not installed on this machine.");
        return;
    }

    createInstanceAsync(description, sampleRate, blockSize, std::move(callback));
}

//==============================================================================
// HostedPluginBackend::ScopedDefault
//==============================================================================

namespace {
// The process-wide override. Message thread only: it is read by getDefault() from
// HostedPluginModule's load/setExtraState paths, both of which are message-thread-only.
HostedPluginBackend* defaultBackendOverride = nullptr;
} // namespace

HostedPluginBackend::ScopedDefault::ScopedDefault(HostedPluginBackend* backend)
    : previous_(defaultBackendOverride) {
    defaultBackendOverride = backend;
}

HostedPluginBackend::ScopedDefault::~ScopedDefault() { defaultBackendOverride = previous_; }

HostedPluginBackend& HostedPluginBackend::getDefault() {
    if (defaultBackendOverride != nullptr)
        return *defaultBackendOverride;

    // Constructed on first use and never destroyed: juce::AudioPluginFormatManager owns format
    // objects that may keep loaded binaries alive, and tearing those down at static-destruction time
    // (after JUCE's own singletons have gone) is a documented way to crash on exit.
    static DefaultHostedPluginBackend* realBackend = new DefaultHostedPluginBackend();
    return *realBackend;
}

//==============================================================================
// DefaultHostedPluginBackend
//==============================================================================

DefaultHostedPluginBackend::DefaultHostedPluginBackend() {
    // TL7-1: JUCE's built-in hosting only. VST3 everywhere, AU additionally on macOS; CLAP deferred.
    // Explicit rather than addDefaultFormats() so the hosted set is a decision in source.
#if JUCE_PLUGINHOST_VST3
    formatManager_.addFormat(new juce::VST3PluginFormat());
#endif
#if JUCE_PLUGINHOST_AU && JUCE_MAC
    formatManager_.addFormat(new juce::AudioUnitPluginFormat());
#endif
}

DefaultHostedPluginBackend::~DefaultHostedPluginBackend() = default;

void DefaultHostedPluginBackend::createInstanceAsync(const juce::PluginDescription& description, double sampleRate,
                                                     int blockSize, InstanceCallback callback) {
    if (callback == nullptr)
        return;

    if (formatManager_.getNumFormats() == 0) {
        failAsync(std::move(callback), "No plugin formats are available in this build.");
        return;
    }

    formatManager_.createPluginInstanceAsync(description, sampleRate, blockSize, std::move(callback));
}

bool DefaultHostedPluginBackend::resolveIdentity(const PluginIdentity& identity, juce::PluginDescription& out) const {
    for (const auto& candidate : knownPlugins_) {
        if (identity.matches(candidate)) {
            out = candidate;
            return true;
        }
    }
    return false;
}

void DefaultHostedPluginBackend::setKnownPlugins(std::vector<juce::PluginDescription> plugins) {
    knownPlugins_ = std::move(plugins);
}

} // namespace synth
