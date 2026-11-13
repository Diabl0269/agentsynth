// ModuleCardLayoutBinding.cpp -- hands the app's per-type layout store to a GraphEditor's cards and
// rebuilds a type's cards when its stored default changes.
// docs/layout/module-card-layout.md#where-a-layout-comes-from.
#include "ModuleCardLayoutBinding.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <set>

namespace synth {

namespace {

// The store rides on the editor's own property set, so no GraphEditor member is needed and the link
// dies with the editor; the store itself is held weakly, so it may die first.
constexpr const char* kStoreProperty = "moduleCardLayoutStore";

struct StoreRef final : juce::ReferenceCountedObject {
    explicit StoreRef(ModuleCardLayoutStore& s)
        : store(&s) {}
    juce::WeakReference<ModuleCardLayoutStore> store;
};

} // namespace

ModuleCardLayoutBinding::ModuleCardLayoutBinding(GraphEditor& editor, ModuleCardLayoutStore& store)
    : editor_(&editor)
    , store_(&store) {
    editor.getProperties().set(kStoreProperty, juce::var(new StoreRef(store)));
    store.addListener(this);
}

ModuleCardLayoutBinding::~ModuleCardLayoutBinding() {
    if (auto* store = store_.get())
        store->removeListener(this);
    if (auto* editor = editor_.getComponent())
        editor->getProperties().remove(kStoreProperty);
}

ModuleCardLayoutStore* findModuleCardLayoutStore(const juce::Component& editor) {
    if (auto* ref = dynamic_cast<StoreRef*>(editor.getProperties()[kStoreProperty].getObject()))
        return ref->store.get();
    return nullptr;
}

// updateComponents rebuilds every card whose body was built from an older revision of this type's
// default (CardBody::isStaleFor); each rebuilt card then makes room for, or gives back, its new height.
// Called synchronously from the store's write, so an editor session that wrote the default records
// the moved neighbours in its own undo step.
void ModuleCardLayoutBinding::layoutChangedForModuleType(const juce::String& moduleType) {
    auto* editor = dynamic_cast<GraphEditor*>(editor_.getComponent());
    if (editor == nullptr)
        return;
    std::set<ModuleComponent*> before;
    for (auto* card : editor->getModuleComponents())
        before.insert(card);
    editor->updateComponents();
    for (auto* card : editor->getModuleComponents())
        if (card != nullptr && before.count(card) == 0 &&
            AIStateMapper::getFactoryTypeName(card->getModule()) == moduleType)
            editor->handleModuleResized(card);
}

} // namespace synth
