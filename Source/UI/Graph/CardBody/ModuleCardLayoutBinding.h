#pragma once

// Connects the app's ModuleCardLayoutStore to a GraphEditor's cards: the cards resolve their
// per-type default against it, and a default written or cleared re-lays out every card of that type.
// docs/layout/module-card-layout.md#where-a-layout-comes-from.

#include "UI/Graph/CardBody/ModuleCardLayoutStore.h"
#include <juce_gui_basics/juce_gui_basics.h>

class GraphEditor;

namespace synth {

/**
 * While alive, `editor`'s cards resolve against `store` (findModuleCardLayoutStore) and a change of a
 * type's default rebuilds that type's cards. Either object may be destroyed first. Message thread only.
 */
class ModuleCardLayoutBinding final : private ModuleCardLayoutStore::Listener {
public:
    ModuleCardLayoutBinding(GraphEditor& editor, ModuleCardLayoutStore& store);
    ~ModuleCardLayoutBinding() override;

private:
    void layoutChangedForModuleType(const juce::String& moduleType) override;

    juce::Component::SafePointer<juce::Component> editor_;
    juce::WeakReference<ModuleCardLayoutStore> store_;

    JUCE_DECLARE_NON_COPYABLE(ModuleCardLayoutBinding)
};

/** The store bound to `editor`, or null (no binding, or the store is gone). */
ModuleCardLayoutStore* findModuleCardLayoutStore(const juce::Component& editor);

} // namespace synth
