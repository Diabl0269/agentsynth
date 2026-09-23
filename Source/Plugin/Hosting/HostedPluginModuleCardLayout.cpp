// HostedPluginModuleCardLayout.cpp -- the per-instance card-layout override on HostedPluginModule:
// the trusted extra-state key "cardLayout" and the layout-only patch undo/redo applies. The type
// that gives the value meaning is CardLayout (Source/Modules/CardLayout.h); this unit stores it as
// opaque JSON so the module needs no knowledge of it.
// See docs/control/plugin-card-layout.md#persistence.
#include "HostedPluginModule.h"

namespace synth {

namespace {
constexpr const char* kCardLayoutKey = "cardLayout";

bool sameJson(const juce::var& a, const juce::var& b) { return juce::JSON::toString(a) == juce::JSON::toString(b); }
} // namespace

// The value is deep-copied on the way in: getExtraState() hands out the same tree to the project
// writer, and a caller that kept mutating its own DynamicObject would otherwise edit the live
// override without a notification. The "changed" test compares serialisations, not identity, so an
// undo that re-applies an equal layout does not make the card rebuild for nothing.
void HostedPluginModule::setCardLayoutOverride(const juce::var& layout) {
    if (sameJson(cardLayoutOverride_, layout))
        return;

    cardLayoutOverride_ = layout.isVoid() ? juce::var() : layout.clone();
    if (onCardLayoutChanged)
        onCardLayoutChanged();
}

// `{ "cardLayout": <layout | null> }` and nothing else. This is what AppUndoManager's node
// extra-state action replays: a full getExtraState() would carry the plugin's state blob and make
// undo re-load the plugin, rewinding whatever the user did inside its editor since.
juce::var HostedPluginModule::makeCardLayoutPatch(const juce::var& layout) {
    auto* object = new juce::DynamicObject();
    object->setProperty(kCardLayoutKey, layout.isVoid() ? juce::var() : layout.clone());
    return juce::var(object);
}

// A patch is layout-only when it has the "cardLayout" key and none of the identity keys. A saved
// project can never look like that (getExtraState() writes the identity whenever it writes
// anything), so the discriminator cannot swallow a real restore.
bool HostedPluginModule::applyCardLayoutPatch(const juce::var& state) {
    auto* object = state.getDynamicObject();
    if (object == nullptr || !object->hasProperty(kCardLayoutKey))
        return false;
    if (object->hasProperty("pluginFormat") || object->hasProperty("pluginName") || object->hasProperty("pluginUid"))
        return false;

    setCardLayoutOverride(object->getProperty(kCardLayoutKey));
    return true;
}

} // namespace synth
