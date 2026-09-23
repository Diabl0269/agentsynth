// HostedPluginCardLayout.cpp -- which layout a hosted-plugin card shows, and how each slot binds to
// the live instance. See docs/control/plugin-card-layout.md#the-cardlayout-type-and-where-a-layout-comes-from.
#include "HostedPluginCardLayout.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "PluginCardLayoutStore.h"
#include "Timeline/AutomationBinding.h"

namespace synth {

namespace {

constexpr int kAutomaticSlotCount = 8;

// The layout an override or stored default names, or nullopt when there is none. An override or
// file that does not parse (corrupt, or written by a newer version) is skipped, not fatal: the next
// source in the precedence chain is used, and the unreadable one is left on disk untouched.
std::optional<CardLayout> layoutFromVar(const juce::var& json) {
    if (json.isVoid())
        return std::nullopt;
    auto parsed = CardLayout::fromVar(json);
    if (parsed.status != CardLayout::ParseStatus::Ok)
        return std::nullopt;
    return std::move(parsed.layout);
}

std::optional<CardLayout> storedDefault(const HostedPluginModule& node, const PluginCardLayoutStore* store) {
    if (store == nullptr || !node.getIdentity().isValid())
        return std::nullopt;
    auto loaded = store->loadDefault(node.getIdentity());
    if (loaded.status != PluginCardLayoutStore::LoadStatus::Ok)
        return std::nullopt;
    return std::move(loaded.layout);
}

bool isBypassLike(const juce::AudioProcessorParameter& param, const juce::AudioProcessorParameter* bypass) {
    return &param == bypass || param.getName(256) == "Bypass";
}

// Binds a slot with the SAME rules an automation lane uses (exact id, then the index hint only for
// a format with no stable ids, otherwise orphan) by going through resolveLaneParameter itself, so
// the two can never drift apart. With no live instance a slot is merely unresolved, not orphaned:
// the plugin may still be loading, and an orphan is dropped on the next save.
ResolvedCardSlot bindSlot(HostedPluginModule& node, const CardSlot& slot) {
    ResolvedCardSlot bound;
    bound.slot = slot;
    if (!node.hasInstance())
        return bound;

    const auto resolution = resolveLaneParameter(&node, slot.paramId, slot.indexHint);
    bound.orphaned = resolution.orphaned;
    bound.param = resolution.rangedParam != nullptr
                      ? static_cast<juce::AudioProcessorParameter*>(resolution.rangedParam)
                      : resolution.hostedParam;
    return bound;
}

} // namespace

int ResolvedCardLayout::orphanCount() const {
    int count = 0;
    for (const auto& resolved : slots)
        count += resolved.orphaned ? 1 : 0;
    return count;
}

CardLayout ResolvedCardLayout::layoutWithoutOrphans() const {
    CardLayout result = layout;
    result.slots.clear();
    for (const auto& resolved : slots)
        if (!resolved.orphaned)
            result.slots.push_back(resolved.slot);
    return result;
}

// Never persisted and always recomputed: a plugin update that reorders its parameters simply yields
// a different automatic set instead of a stale file.
CardLayout automaticCardLayout(const HostedPluginModule& node) {
    CardLayout layout;
    auto* instance = node.getActiveInstanceForEditor();
    if (instance == nullptr)
        return layout;

    const juce::AudioProcessorParameter* bypass = instance->getBypassParameter();
    for (const auto& info : node.getInstanceParameters()) {
        if (static_cast<int>(layout.slots.size()) >= kAutomaticSlotCount)
            break;
        auto* param = node.findInstanceParameterByIndex(info.index);
        if (param == nullptr || !param->isAutomatable() || isBypassLike(*param, bypass))
            continue;

        CardSlot slot;
        slot.paramId = info.paramId;
        slot.indexHint = info.index;
        layout.slots.push_back(std::move(slot));
    }
    return layout;
}

ResolvedCardLayout resolveHostedCardLayout(HostedPluginModule& node, const PluginCardLayoutStore* store) {
    ResolvedCardLayout resolved;

    if (auto layout = layoutFromVar(node.getCardLayoutOverride())) {
        resolved.source = ResolvedCardLayout::Source::Instance;
        resolved.layout = std::move(*layout);
    } else if (auto stored = storedDefault(node, store)) {
        resolved.source = ResolvedCardLayout::Source::PluginDefault;
        resolved.layout = std::move(*stored);
    } else {
        resolved.source = ResolvedCardLayout::Source::Automatic;
        resolved.layout = automaticCardLayout(node);
    }

    resolved.slots.reserve(resolved.layout.slots.size());
    for (const auto& slot : resolved.layout.slots)
        resolved.slots.push_back(bindSlot(node, slot));
    return resolved;
}

} // namespace synth
