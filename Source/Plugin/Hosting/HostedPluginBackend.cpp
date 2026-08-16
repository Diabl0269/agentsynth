#include "HostedPluginBackend.h"
#include "PluginScanService.h"

namespace synth {

//==============================================================================
// Hosted format set — the single declaration of what this app loads and scans
//==============================================================================

void addHostedPluginFormats(juce::AudioPluginFormatManager& manager) {
#if JUCE_PLUGINHOST_VST3
    manager.addFormat(new juce::VST3PluginFormat());
#endif
#if JUCE_PLUGINHOST_AU && JUCE_MAC
    manager.addFormat(new juce::AudioUnitPluginFormat());
#endif
    juce::ignoreUnused(manager);
}

juce::StringArray hostedPluginFormatNames() {
    // Asked of the format objects rather than spelled out, so the names a scan is driven with are by
    // construction the names PluginDescription::pluginFormatName will carry back.
    juce::AudioPluginFormatManager manager;
    addHostedPluginFormats(manager);

    juce::StringArray names;
    for (auto* format : manager.getFormats())
        if (format != nullptr)
            names.add(format->getName());
    return names;
}

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

juce::String PluginIdentity::toDragPayload() const {
    return juce::String(kDragPayloadPrefix) + format + "|" + juce::String(uid) + "|" + name;
}

bool PluginIdentity::isDragPayload(const juce::String& payload) { return payload.startsWith(kDragPayloadPrefix); }

PluginIdentity PluginIdentity::fromDragPayload(const juce::String& payload) {
    PluginIdentity identity;
    if (!isDragPayload(payload))
        return identity;

    const auto body = payload.fromFirstOccurrenceOf(kDragPayloadPrefix, false, false);
    identity.format = body.upToFirstOccurrenceOf("|", false, false);

    const auto afterFormat = body.fromFirstOccurrenceOf("|", false, false);
    identity.uid = afterFormat.upToFirstOccurrenceOf("|", false, false).getIntValue();
    // Name last and unsplit: a plugin called "Sub | Bass" must survive the round trip.
    identity.name = afterFormat.fromFirstOccurrenceOf("|", false, false);
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

DefaultHostedPluginBackend::DefaultHostedPluginBackend() { addHostedPluginFormats(formatManager_); }

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
    // Scan list first — it is the live picture of what is installed, and it applies the documented
    // uid-then-name precedence (PluginScanService::resolve). knownPlugins_ is the fallback for a
    // backend nobody installed a service on.
    if (scanService_ != nullptr) {
        if (const auto resolved = scanService_->resolve(identity)) {
            out = *resolved;
            return true;
        }
    }

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
