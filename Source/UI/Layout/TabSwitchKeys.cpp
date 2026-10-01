#include "UI/Layout/TabSwitchKeys.h"

#include <algorithm>

// Concern: offering the tab-switching keys to a tab handler ahead of a text field's own key handling
// (see the header).

namespace synth::ui {

TabSwitchKeys::TabSwitchKeys(Handler handler)
    : handler_(std::move(handler)) {}

TabSwitchKeys::~TabSwitchKeys() {
    for (auto& component : attached_)
        if (component != nullptr)
            component->removeKeyListener(this);
}

bool TabSwitchKeys::isAttachedForTest(const juce::Component* component) const {
    return std::any_of(attached_.begin(), attached_.end(),
                       [component](const auto& entry) { return entry.getComponent() == component; });
}

void TabSwitchKeys::attachTo(juce::Component& component) {
    if (isAttachedForTest(&component))
        return;
    component.addKeyListener(this);
    attached_.emplace_back(&component);
}

void TabSwitchKeys::attachToTextEditorsIn(juce::Component& root) {
    if (dynamic_cast<juce::TextEditor*>(&root) != nullptr)
        attachTo(root);
    for (auto* child : root.getChildren())
        attachToTextEditorsIn(*child);
}

bool TabSwitchKeys::keyPressed(const juce::KeyPress& key, juce::Component* /*originatingComponent*/) {
    return handler_ && handler_(key);
}

int TabSwitchKeys::positionalTabIndex(const juce::KeyPress& key) {
    if (key.getModifiers() != juce::ModifierKeys(juce::ModifierKeys::commandModifier))
        return -1;
    const int code = key.getKeyCode();
    return code >= '1' && code <= '9' ? code - '1' : -1;
}

} // namespace synth::ui
