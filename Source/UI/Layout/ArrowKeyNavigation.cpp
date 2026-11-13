#include "UI/Layout/ArrowKeyNavigation.h"

#include <algorithm>
#include <typeinfo>

// Concern: list-style arrow keys over a scope of controls (see the header).

namespace synth::ui {

namespace {
void collectPlainViewports(juce::Component& root, std::vector<juce::Viewport*>& out) {
    for (auto* child : root.getChildren()) {
        // Exact type: a ListBox's or TextEditor's own viewport is a subclass, and that control owns its arrows.
        if (auto* viewport = dynamic_cast<juce::Viewport*>(child);
            viewport != nullptr && typeid(*viewport) == typeid(juce::Viewport))
            out.push_back(viewport);
        collectPlainViewports(*child, out);
    }
}
} // namespace

ArrowKeyNavigation::ArrowKeyNavigation(juce::Component& scope)
    : scope_(&scope) {
    hooks_.focused = [] { return juce::Component::getCurrentlyFocusedComponent(); };
    hooks_.moveFocusTo = [](juce::Component& c) { c.grabKeyboardFocus(); };
    scope.addKeyListener(this);
    listeningOn_.emplace_back(&scope);
}

ArrowKeyNavigation::~ArrowKeyNavigation() {
    for (auto& component : listeningOn_)
        if (component != nullptr)
            component->removeKeyListener(this);
}

void ArrowKeyNavigation::watchViewport(juce::Viewport& viewport) {
    if (isListeningOnForTest(&viewport))
        return;
    viewport.addKeyListener(this);
    listeningOn_.emplace_back(&viewport);
}

void ArrowKeyNavigation::watchViewportsInScope() {
    if (scope_ == nullptr)
        return;
    std::vector<juce::Viewport*> viewports;
    collectPlainViewports(*scope_, viewports);
    for (auto* viewport : viewports)
        watchViewport(*viewport);
}

void ArrowKeyNavigation::setFocusHooksForTest(FocusHooks hooks) {
    if (hooks.focused)
        hooks_.focused = std::move(hooks.focused);
    if (hooks.moveFocusTo)
        hooks_.moveFocusTo = std::move(hooks.moveFocusTo);
}

bool ArrowKeyNavigation::isListeningOnForTest(const juce::Component* component) const {
    return std::any_of(listeningOn_.begin(), listeningOn_.end(),
                       [component](const auto& entry) { return entry.getComponent() == component; });
}

// The same list Tab walks (juce::KeyboardFocusTraverser), minus a control with no bounds: it is a
// Tab stop nobody can see, and an arrow key must never land on it.
std::vector<juce::Component*> ArrowKeyNavigation::focusStops() const {
    std::vector<juce::Component*> stops;
    if (scope_ == nullptr)
        return stops;
    juce::KeyboardFocusTraverser traverser;
    for (auto* c : traverser.getAllComponents(scope_.getComponent()))
        if (c->isVisible() && c->isEnabled() && !c->getLocalBounds().isEmpty())
            stops.push_back(c);
    return stops;
}

bool ArrowKeyNavigation::keyPressed(const juce::KeyPress& key, juce::Component* /*originatingComponent*/) {
    // The key listener's second argument is the component the listener sits on, not the one with
    // focus, so the focused control comes from the hook.
    if (scope_ == nullptr || key.getModifiers().isAnyModifierKeyDown())
        return false;
    const int code = key.getKeyCode();
    const bool vertical = code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
    const bool horizontal = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey;
    if (!vertical && !horizontal)
        return false;

    auto* focused = hooks_.focused ? hooks_.focused() : nullptr;
    if (focused == nullptr || focused == scope_.getComponent() || !scope_->isParentOf(focused))
        return false;

    if (vertical)
        return moveFocus(*focused, code == juce::KeyPress::downKey ? 1 : -1);
    return handleHorizontal(*focused, code == juce::KeyPress::rightKey);
}

bool ArrowKeyNavigation::moveFocus(juce::Component& from, int direction) {
    const auto stops = focusStops();
    // Focus can sit on a part of a stop (a sub-component); the stop is its nearest ancestor in the list.
    auto it = stops.end();
    for (auto* c = &from; c != nullptr && c != scope_.getComponent() && it == stops.end(); c = c->getParentComponent())
        it = std::find(stops.begin(), stops.end(), c);
    if (it == stops.end())
        return false;

    const auto index = static_cast<int>(it - stops.begin()) + direction;
    // At either end the key is still consumed: it must not scroll the viewport or wrap round.
    if (index >= 0 && index < static_cast<int>(stops.size()) && hooks_.moveFocusTo)
        hooks_.moveFocusTo(*stops[static_cast<size_t>(index)]);
    return true;
}

// A fold header folds on Left and unfolds on Right; a check box goes off on Left and on on Right,
// through setToggleState(sendNotification), which is exactly what a click does (the click callback
// persists the setting and records the undo step). Idempotent. A radio button cannot be switched off.
bool ArrowKeyNavigation::handleHorizontal(juce::Component& focused, bool rightKey) {
    if (auto* header = dynamic_cast<FoldableHeader*>(&focused)) {
        header->setFolded(!rightKey);
        return true;
    }
    if (auto* toggle = dynamic_cast<juce::ToggleButton*>(&focused)) {
        if (!toggle->getClickingTogglesState())
            return false;
        if (!rightKey && toggle->getRadioGroupId() != 0)
            return false;
        if (toggle->getToggleState() != rightKey)
            toggle->setToggleState(rightKey, juce::sendNotification);
        return true;
    }
    return false;
}

} // namespace synth::ui
