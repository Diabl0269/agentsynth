#pragma once

// RealKeyDelivery.h -- delivers a key the way the operating system does, for the sweeps that press keys on every
// control: the component that holds keyboard focus (or the topmost modal one, an open menu) gets it through its own
// window's juce::ComponentPeer::handleKeyPress, which offers it to key listeners and then to keyPressed, from that
// component up its parents. Nothing here calls a component's keyPressed directly, so the app's own dispatch
// (MainComponent::keyPressed, the dialogs' close and relay listeners, a popup menu's Escape) is what answers.
//
//   takeFocus(c)            gives `c` keyboard focus and waits for it (a native window hands focus over later).
//   deliver(c, key)         presses `key` with `c` focused; says whether it was handled and by which component.
//   DesktopWatch            the top-level windows opened since it was made (a menu, a popup, a dialog).
//
// "By whom": deliver() puts a passive key listener on the focused component and each of its parents for the length of
// the call. The peer offers a key to the listeners of one component before its keyPressed, from the focused one
// upward and stops at the first that handles it, so the last component whose listener fired is the one that handled
// the key (or the top of the chain when nothing did).
//
// The window of the component must be on the desktop (a display is needed); a caller skips when there is none.
#include "../../TestWait.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <set>
#include <vector>

namespace synth::test::real_keys {

inline const juce::KeyPress kSpace(juce::KeyPress::spaceKey, juce::ModifierKeys(), ' ');
inline const juce::KeyPress kReturn(juce::KeyPress::returnKey, juce::ModifierKeys(), 0);
inline const juce::KeyPress kEscape(juce::KeyPress::escapeKey, juce::ModifierKeys(), 0);
inline const juce::KeyPress kUndo('z', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0);

struct Delivery {
    bool handled = false;
    // Null when nothing handled the key, or when the handler is gone again (a window the key closed).
    juce::Component::SafePointer<juce::Component> handledBy;
    juce::Component::SafePointer<juce::Component> origin; // where the key started: the focused component, or a menu
};

/** Gives `c` keyboard focus and waits for it. True when it has it. */
inline bool takeFocus(juce::Component& c) {
    if (c.hasKeyboardFocus(false))
        return true;
    // JUCE only hands focus to a shown, enabled component that wants it: waiting a second on one that cannot is waste.
    if (!c.isShowing() || !c.isEnabled() || !c.getWantsKeyboardFocus())
        return false;
    return synth::test::waitUntil(
        [&c] {
            if (!c.hasKeyboardFocus(false))
                c.grabKeyboardFocus();
            return c.hasKeyboardFocus(false);
        },
        1000, 20);
}

namespace detail {

class Probe : public juce::KeyListener {
public:
    Probe(juce::Component& owner, juce::Component::SafePointer<juce::Component>& last)
        : owner_(&owner)
        , last_(last) {}
    bool keyPressed(const juce::KeyPress&, juce::Component*) override {
        last_ = owner_;
        return false;
    }

private:
    juce::Component* owner_;
    juce::Component::SafePointer<juce::Component>& last_;
};

} // namespace detail

/** Presses `press` with `focused` holding keyboard focus (call takeFocus first), then runs the messages it posted. */
inline Delivery deliver(juce::Component& focused, const juce::KeyPress& press) {
    Delivery result;
    juce::Component* start = juce::Component::getCurrentlyFocusedComponent();
    if (auto* modal = juce::Component::getCurrentlyModalComponent(0);
        modal != nullptr && modal != start && !modal->isParentOf(start))
        start = modal;
    if (start == nullptr)
        start = &focused;
    result.origin = start;
    auto* peer = start->getPeer();
    if (peer == nullptr)
        return result;

    juce::Component::SafePointer<juce::Component> last;
    std::vector<juce::Component::SafePointer<juce::Component>> chain;
    std::vector<std::unique_ptr<detail::Probe>> probes;
    for (auto* c = start; c != nullptr; c = c->getParentComponent()) {
        chain.emplace_back(c);
        probes.push_back(std::make_unique<detail::Probe>(*c, last));
        c->addKeyListener(probes.back().get());
    }
    result.handled = peer->handleKeyPress(press);
    for (size_t i = 0; i < chain.size(); ++i)
        if (chain[i] != nullptr) // a window a key closed is already gone
            chain[i]->removeKeyListener(probes[i].get());
    if (result.handled)
        result.handledBy = last;
    synth::test::flushMessageQueue();
    return result;
}

/** The top-level windows that were not there when the watch was made. */
class DesktopWatch {
public:
    DesktopWatch() { before_ = current(); }
    std::vector<juce::Component*> opened() const {
        std::vector<juce::Component*> found;
        for (auto* c : current())
            if (before_.count(c) == 0 && c->isVisible())
                found.push_back(c);
        return found;
    }

private:
    static std::set<juce::Component*> current() {
        std::set<juce::Component*> all;
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            all.insert(desktop.getComponent(i));
        return all;
    }
    std::set<juce::Component*> before_;
};

} // namespace synth::test::real_keys
