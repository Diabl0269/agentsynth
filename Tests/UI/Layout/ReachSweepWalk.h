#pragma once

// ReachSweepWalk.h -- the walker of ReachSweepTests.cpp: it presses real keys (RealKeyDelivery.h) in one focus region
// of a real MainComponent and judges where focus lands after every key.
//
// Per region: focus it through the registry, then explore. From each place focus has landed in the region, each of
// Down, Up, Left, Right, Tab, Shift+Tab and Escape is pressed (focus is put back on the place before each key; Return
// and Space are never pressed, they activate things). A ring source (the toolbars' roving ring, a panel's top bar
// KeyRing, the mixer's focused column, the zones pane's cursor row) that shows after a key is walked with Right to its
// end and left with Down. The region's first landing then gets the global keys (Cmd+1..4, Cmd+Up, Cmd+Shift+Up), each
// followed by the four arrows. A key that opens a menu, popup or dialog is not judged: it is closed.
//
// After every key: A) focus is in some region; B) the focus owner draws something (a ring source of its region shows,
// or a pixel check: the region root rendered with focus on the owner and again with focus on a neutral helper differ
// around the owner); C) no ring source shows while focus is outside its owner; E) Tab and Shift+Tab that leave the
// region land in the registry's next open region. After the walk: D) every stop of the region (focusable descendants
// that show, and every ring source's stops) was a focus owner or a ringed stop.
#include "../../TestWait.h"
#include "KeyboardSweepShell.h"
#include "MainComponent/MainComponent.h"
#include "ReachSweepRings.h"
#include "RealKeyDelivery.h"
#include "UI/Chrome/CanvasToolbar/CanvasToolbar.h"
#include "UI/Chrome/ToolbarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Layout/KeyRing.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Library/LibrarySidebarViews.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace synth::test::reach {

using synth::test::keyboard_sweep::closeMenus;
using synth::test::keyboard_sweep::describe;
using synth::test::keyboard_sweep::SweepFindings;
using namespace synth::test::real_keys;

namespace detail {

inline std::string buttonName(juce::Component& c) {
    if (c.getTitle().isNotEmpty())
        return c.getTitle().toStdString();
    if (auto* b = dynamic_cast<juce::Button*>(&c))
        return b->getButtonText().toStdString();
    return {};
}

inline std::vector<juce::Component*> buttonsOf(juce::Component& root, bool siblings) {
    std::vector<juce::Component*> found;
    auto* parent = siblings ? root.getParentComponent() : &root;
    if (parent == nullptr)
        return found;
    for (auto* child : parent->getChildren())
        if (dynamic_cast<juce::Button*>(child) != nullptr && child->isVisible() && child->isEnabled() &&
            (!siblings || root.getBounds().contains(child->getBounds().getCentre())))
            found.push_back(child);
    return found;
}

// The toolbars' roving ring: it paints while the strip itself holds focus, on the slot getFocusedButtonName names.
template <typename Strip>
RingSource buttonStrip(Strip& strip, bool buttonsAreSiblings) {
    RingSource s;
    s.name = "strip";
    s.owner = &strip;
    s.active = [&strip] { return strip.hasKeyboardFocus(false) && strip.getFocusedSlot() >= 0; };
    s.stops = [&strip, buttonsAreSiblings] { return buttonsOf(strip, buttonsAreSiblings); };
    s.ringed = [&strip, buttonsAreSiblings]() -> juce::Component* {
        const auto name = strip.getFocusedButtonName().toStdString();
        for (auto* c : buttonsOf(strip, buttonsAreSiblings))
            if (buttonName(*c) == name)
                return c;
        return nullptr;
    };
    return s;
}

} // namespace detail

inline std::vector<RingSource> ringSourcesOf(const synth::ui::FocusRegion& region) {
    std::vector<RingSource> sources;
    if (region.root == nullptr)
        return sources;
    if (region.id == "toolbar")
        sources.push_back(detail::buttonStrip(static_cast<ToolbarComponent&>(*region.root), true));
    else if (region.id == "canvasToolbar")
        sources.push_back(detail::buttonStrip(static_cast<synth::ui::CanvasToolbar&>(*region.root), false));
    else if (region.id == "timeline") {
        auto& panel = static_cast<synth::ui::TimelinePanelComponent&>(*region.root);
        sources.push_back(detail::keyRingSource("top bar", panel.getTopBarKeysForTest(), panel));
    } else if (region.id == "mixer") {
        for (auto& source : mixerRingSources(*region.root))
            sources.push_back(std::move(source));
    }
    return sources;
}

struct KeySpec {
    std::string name;
    juce::KeyPress press;
    int tab = 0; // +1 Tab, -1 Shift+Tab
};

inline const KeySpec kDown{"Down", juce::KeyPress(juce::KeyPress::downKey)};
inline const KeySpec kUp{"Up", juce::KeyPress(juce::KeyPress::upKey)};
inline const KeySpec kLeft{"Left", juce::KeyPress(juce::KeyPress::leftKey)};
inline const KeySpec kRight{"Right", juce::KeyPress(juce::KeyPress::rightKey)};
inline const KeySpec kTab{"Tab", juce::KeyPress(juce::KeyPress::tabKey), 1};
inline const KeySpec kShiftTab{"Shift+Tab",
                               juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0), -1};
inline const KeySpec kHome{"Home", juce::KeyPress(juce::KeyPress::homeKey)};
inline const KeySpec kEsc{"Escape", kEscape};
inline const KeySpec kReturn{"Return", juce::KeyPress(juce::KeyPress::returnKey)};

inline std::vector<KeySpec> globalKeys() {
    const juce::ModifierKeys cmd(juce::ModifierKeys::commandModifier);
    std::vector<KeySpec> keys;
    for (int n = 1; n <= 4; ++n)
        keys.push_back({"Cmd+" + std::to_string(n), juce::KeyPress('0' + n, cmd, 0)});
    keys.push_back({"Cmd+Up", juce::KeyPress(juce::KeyPress::upKey, cmd, 0)}); // the mixer's top bar ring
    keys.push_back(
        {"Cmd+Shift+Up", juce::KeyPress(juce::KeyPress::upKey, cmd.withFlags(juce::ModifierKeys::shiftModifier), 0)});
    return keys;
}

class ReachWalker {
public:
    static constexpr int kKeyCap = 400;
    static constexpr int kRingCap = 40;

    // `restoreScene` rebuilds the scene (main view, dock, panes); the walker calls it when a key changed which regions
    // are open. `helper` is a focusable component of the main window outside every region.
    ReachWalker(MainComponent& mc, juce::Component& helper, SweepFindings& findings, std::string scene,
                std::function<void()> restoreScene)
        : mc_(mc)
        , helper_(helper)
        , findings_(findings)
        , scene_(std::move(scene))
        , restoreScene_(std::move(restoreScene)) {
        shape_ = sceneShape();
    }

    void walkAll() {
        std::vector<juce::String> ids;
        for (const auto* r : registry().openRegions())
            ids.push_back(r->id);
        findings_.walked(scene_, ids.size());
        for (const auto& id : ids)
            walkRegion(id);
        std::cout << "[ reach ] " << scene_ << ": " << totalKeys_ << " keys, " << restores_ << " scene restores, "
                  << unjudgedBounds_ << " owners not measured\n";
    }

private:
    struct Walk {
        juce::String id;
        int keys = 0;
        std::set<juce::Component*> reached;
        std::set<juce::Component*> visited;
        std::set<std::string> ringsWalked;
        std::deque<juce::Component::SafePointer<juce::Component>> queue;
        std::vector<std::string> log;
    };

    synth::ui::FocusRegionRegistry& registry() { return mc_.getFocusRegionsForTest(); }
    bool verbose() const { return std::getenv("AGENTSYNTH_REACH_VERBOSE") != nullptr; }

    std::string sceneShape() {
        std::string shape;
        for (const auto* r : registry().openRegions())
            shape += r->id.toStdString() + ",";
        shape += std::to_string((int)mc_.getBottomDock().getMainView());
        // Left and Right on the library's switch change its view, and with it which stops there are.
        shape += std::to_string((int)mc_.getLibrarySidebarForTest().getView());
        return shape;
    }

    void keepSceneIntact() {
        if (sceneShape() == shape_)
            return;
        if (verbose())
            std::cout << "[ reach ] scene changed: " << shape_ << " -> " << sceneShape() << "\n";
        restoreScene_();
        synth::test::flushMessageQueue();
        ++restores_;
    }

    bool inRegion(juce::Component* c, const juce::String& id) {
        const auto* r = registry().regionContaining(c);
        return r != nullptr && r->id == id;
    }

    void closeTransients(const DesktopWatch& watch) {
        closeMenus();
        synth::ui::deleteOpenModalDialogs();
        synth::test::flushMessageQueue();
        if (!watch.opened().empty())
            if (auto* f = juce::Component::getCurrentlyFocusedComponent())
                deliver(*f, kEscape);
        synth::test::flushMessageQueue();
    }

    // Presses `key` with `from` focused and judges where focus lands. False when it opened a window (closed, not
    // judged).
    bool press(Walk& w, juce::Component& from, const KeySpec& key) {
        ++w.keys;
        ++totalKeys_;
        const auto prev = describe(from);
        DesktopWatch watch;
        deliver(from, key.press);
        if (!watch.opened().empty()) {
            closeTransients(watch);
            return false;
        }
        judge(w, key, prev, &from);
        return true;
    }

    void judge(Walk& w, const KeySpec& key, const std::string& prev, juce::Component* from) {
        auto* f = juce::Component::getCurrentlyFocusedComponent();
        const auto detail = "key " + key.name + ", previous stop " + prev;
        const auto* landed = registry().regionContaining(f);
        if (verbose())
            std::cout << "[ reach ] " << scene_ << " / " << w.id << ": " << key.name << " from " << prev << " -> "
                      << (f != nullptr ? describe(*f) : "nothing") << " in " << (landed ? landed->id : "no region")
                      << "\n";
        checkStale(f, detail);
        if (landed == nullptr) {
            add(w.id.toStdString(), f != nullptr ? describe(*f) : "(nothing)", "focus is in no region", detail);
            return;
        }
        if (landed->id == w.id)
            w.reached.insert(f);
        checkRing(*landed, *f, detail);
        if (key.tab != 0 && landed->id != w.id && from != nullptr && inRegion(from, w.id)) {
            const auto expected = registry().nextOpenRegionId(w.id, key.tab > 0);
            if (landed->id != expected)
                add(w.id.toStdString(), prev,
                    (key.tab > 0 ? "Tab" : "Shift+Tab") + std::string(" leaves to ") + landed->id.toStdString() +
                        " not " + expected.toStdString(),
                    detail);
        }
    }

    // The key of a finding is "<scene> / <region id> / <control>: <what>"; the known-gap check walks scenes.
    void add(const std::string& regionId, const std::string& control, const std::string& what,
             const std::string& detail) {
        findings_.add(scene_ + " / " + regionId, control, what, detail);
    }

    void checkStale(juce::Component* f, const std::string& detail) {
        for (const auto* r : registry().openRegions())
            for (auto& src : ringSourcesOf(*r))
                if (src.active() && (f == nullptr || (f != src.owner && !src.owner->isParentOf(f))))
                    add(r->id.toStdString(), describe(*src.owner), "stale ring (" + src.name + ")", detail);
    }

    bool ringShowsFor(const synth::ui::FocusRegion& region, juce::Component& f) {
        for (auto& src : ringSourcesOf(region))
            if (src.active() && (&f == src.owner || src.owner->isParentOf(&f)))
                return true;
        return false;
    }

    void checkRing(const synth::ui::FocusRegion& region, juce::Component& f, const std::string& detail) {
        if (ringShowsFor(region, f))
            return;
        auto it = drawn_.find(&f);
        if (it == drawn_.end())
            it = drawn_.emplace(&f, drawsSomething(region, f)).first;
        if (!it->second)
            add(region.id.toStdString(), describe(f), "lands on a stop that draws no ring", detail);
    }

    static juce::Image snapshot(juce::Component& root) {
        juce::Image image(juce::Image::ARGB, std::max(1, root.getWidth()), std::max(1, root.getHeight()), true,
                          juce::SoftwareImageType());
        juce::Graphics g(image);
        root.paintEntireComponent(g, false);
        return image;
    }

    // True when a pixel of `area` is the same in two renders with focus on the stop (`a`, `a2`: so not an animation
    // in flight) and differs from the render without it (`b`).
    // Without `settledOnly`: true when a pixel of `area` is the same in two renders with focus on the stop (`a`, `a2`:
    // so not an animation in flight) and differs from the render without it (`b`). With it: true when `a` and `a2`
    // differ anywhere in `area` (still moving).
    static bool differ(const juce::Image& a, const juce::Image& a2, const juce::Image& b, juce::Rectangle<int> area,
                       bool settledOnly = false) {
        area = area.getIntersection(a.getBounds()).getIntersection(a2.getBounds());
        if (!settledOnly)
            area = area.getIntersection(b.getBounds());
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x) {
                if (settledOnly ? a.getPixelAt(x, y) != a2.getPixelAt(x, y)
                                : a.getPixelAt(x, y) == a2.getPixelAt(x, y) && a.getPixelAt(x, y) != b.getPixelAt(x, y))
                    return true;
            }
        return false;
    }

    // Renders the region with focus on `f` and with focus on the helper; true when they differ around `f`.
    bool drawsSomething(const synth::ui::FocusRegion& region, juce::Component& f) {
        auto& root = *region.root;
        const bool isRoot = &f == &root;
        // The whole window is drawn: a proxy root's ring is painted by its parent. A child's area leaves out the
        // region outline, a band along the root's edges.
        const auto rootArea = mc_.getLocalArea(&root, root.getLocalBounds());
        const auto area =
            isRoot ? rootArea.expanded(4)
                   : mc_.getLocalArea(&f, f.getLocalBounds()).expanded(4).getIntersection(rootArea.reduced(3));
        if (area.isEmpty() || root.getWidth() <= 0 || !f.hasKeyboardFocus(false)) {
            ++unjudgedBounds_;
            return true;
        }
        // A stop inside something fading in or out (the clip lanes while a clip editor opens) has no steady picture.
        for (auto* c = &f; c != nullptr; c = c->getParentComponent())
            if (c->getAlpha() < 1.0f) {
                ++unjudgedBounds_;
                return true;
            }
        // A ring that fades in settles first, so the two renders with focus agree.
        auto with = snapshot(mc_);
        auto withAgain = snapshot(mc_);
        synth::test::waitUntil(
            [&] {
                std::swap(with, withAgain);
                withAgain = snapshot(mc_);
                return !differ(with, withAgain, juce::Image(), area, true);
            },
            600, 50);
        if (!takeFocus(helper_) || !helper_.hasKeyboardFocus(false)) {
            ++unjudgedBounds_;
            refocus(f);
            return true;
        }
        const auto without = snapshot(mc_);
        refocus(f);
        return differ(with, withAgain, without, area);
    }

    void enqueueLanding(Walk& w) {
        auto* f = juce::Component::getCurrentlyFocusedComponent();
        if (f != nullptr && inRegion(f, w.id) && w.visited.insert(f).second)
            w.queue.emplace_back(f);
    }

    // Walks a ring that shows: Right until the ringed stop repeats, then Down, and queues where focus lands.
    void walkRing(Walk& w, const std::string& sourceName) {
        const auto* region = registry().findById(w.id);
        if (region == nullptr)
            return;
        // The ring sits where it was last left: Home first, so the walk covers every stop.
        if (auto* f = juce::Component::getCurrentlyFocusedComponent())
            press(w, *f, kHome);
        for (int step = 0; step < kRingCap && w.keys < kKeyCap; ++step) {
            const auto* r = registry().findById(w.id);
            RingSource* source = nullptr;
            auto sources = r != nullptr ? ringSourcesOf(*r) : std::vector<RingSource>{};
            for (auto& s : sources)
                if (s.name == sourceName)
                    source = &s;
            auto* f = juce::Component::getCurrentlyFocusedComponent();
            if (source == nullptr || f == nullptr || !source->active())
                break;
            auto* before = source->ringed();
            if (before != nullptr)
                w.reached.insert(before);
            // The zones list steps by Down: its Right is the way back to the strips.
            if (!press(w, *f, sourceName == "zones cursor" ? kDown : kRight))
                break;
            auto* after = source->ringed();
            if (verbose())
                std::cout << "[ reach ] ring " << sourceName << ": " << (before ? describe(*before) : "none") << " -> "
                          << (after ? describe(*after) : "none") << "\n";
            if (after != nullptr)
                w.reached.insert(after);
            if (!source->active() || after == before)
                break;
        }
        if (auto* f = juce::Component::getCurrentlyFocusedComponent(); f != nullptr && w.keys < kKeyCap)
            press(w, *f, kDown);
        enqueueLanding(w);
    }

    void walkRingsThatShow(Walk& w) {
        const auto* region = registry().findById(w.id);
        if (region == nullptr)
            return;
        auto* f = juce::Component::getCurrentlyFocusedComponent();
        for (auto& src : ringSourcesOf(*region)) {
            if (!src.active() || f == nullptr || (f != src.owner && !src.owner->isParentOf(f)))
                continue;
            if (w.ringsWalked.insert(src.name).second)
                walkRing(w, src.name);
        }
    }

    // Return enters a container (a region root or any stop that is not a plain widget); a widget's own Return is its
    // click, which the walk leaves to the key standards sweep.
    bool takesReturn(juce::Component& place, const juce::String& regionId) {
        const auto* r = registry().findById(regionId);
        if (r != nullptr && r->root == &place)
            return true;
        return dynamic_cast<juce::Button*>(&place) == nullptr && dynamic_cast<juce::ComboBox*>(&place) == nullptr &&
               dynamic_cast<juce::Slider*>(&place) == nullptr && dynamic_cast<juce::TextEditor*>(&place) == nullptr &&
               place.findParentComponentOfClass<juce::Button>() == nullptr &&
               place.findParentComponentOfClass<juce::ComboBox>() == nullptr &&
               place.findParentComponentOfClass<juce::Slider>() == nullptr &&
               place.findParentComponentOfClass<juce::TextEditor>() == nullptr;
    }

    // The canvas keeps keyboard focus on itself while arrows select cards; Return enters the selected card. Each
    // visible card is selected and entered in turn so every card's controls are walked.
    void enterEachCard(Walk& w) {
        auto& editor = mc_.getGraphEditor();
        if (!inRegion(&editor, w.id))
            return;
        std::vector<juce::AudioProcessorGraph::NodeID> ids;
        for (auto* card : editor.getModuleComponents())
            if (card != nullptr && card->getModule() != nullptr && card->isVisible())
                ids.push_back(card->getNodeId());
        if (verbose())
            std::cout << "[ reach ] " << scene_ << " / " << w.id << ": entering " << ids.size() << " cards\n";
        for (auto id : ids) {
            if (w.keys >= kKeyCap)
                break;
            editor.selectModule(id, false);
            if (!takeFocus(editor))
                break;
            press(w, editor, kReturn);
            // Walk the card now: a later selection change rebuilds its controls.
            auto rest = std::move(w.queue);
            w.queue.clear();
            enqueueLanding(w);
            walkRingsThatShow(w);
            exploreQueue(w);
            w.queue = std::move(rest);
            keepSceneIntact();
        }
    }

    // A stop a key already reached gets focus back from the walk even when it does not ask for focus by itself (a
    // card's header buttons take it only through the card's own keyboard entry).
    static bool refocus(juce::Component& c) {
        if (takeFocus(c))
            return true;
        if (c.isShowing() && c.isEnabled())
            c.grabKeyboardFocus();
        synth::test::flushMessageQueue();
        return c.hasKeyboardFocus(false);
    }

    void explore(Walk& w) {
        enterEachCard(w);
        exploreQueue(w);
    }

    void exploreQueue(Walk& w) {
        const KeySpec* keys[] = {&kDown, &kUp, &kLeft, &kRight, &kTab, &kShiftTab, &kEsc, &kReturn};
        while (!w.queue.empty() && w.keys < kKeyCap) {
            auto place = w.queue.front();
            w.queue.pop_front();
            for (const auto* key : keys) {
                if (w.keys >= kKeyCap)
                    break;
                if (place == nullptr || !refocus(*place)) {
                    if (verbose())
                        std::cout << "[ reach ] " << scene_ << " / " << w.id << ": cannot refocus "
                                  << (place == nullptr ? "(gone)" : describe(*place)) << "\n";
                    break;
                }
                if (key == &kReturn && !takesReturn(*place, w.id))
                    continue;
                // Left leaves the first strip for the side pane; the focused column is wherever the walk left it, so
                // Left runs on to the first strip (not judged: it stays on the panel).
                if (key == &kLeft && w.id == "mixer")
                    for (int i = 0; i < 40 && juce::Component::getCurrentlyFocusedComponent() == place; ++i)
                        deliver(*place, key->press);
                if (key == &kLeft && w.id == "mixer" && juce::Component::getCurrentlyFocusedComponent() != place &&
                    !refocus(*place))
                    break;
                const bool judged = press(w, *place, *key);
                if (judged) {
                    enqueueLanding(w);
                    walkRingsThatShow(w);
                }
                keepSceneIntact();
            }
        }
        if (w.keys >= kKeyCap)
            std::cout << "[ reach ] " << scene_ << " / " << w.id << ": key cap " << kKeyCap << " hit\n";
    }

    void globalKeysFrom(Walk& w) {
        for (const auto& global : globalKeys()) {
            if (!registry().focusRegionById(w.id))
                return;
            synth::test::flushMessageQueue();
            auto* first = juce::Component::getCurrentlyFocusedComponent();
            if (first == nullptr || !inRegion(first, w.id))
                continue;
            if (press(w, *first, global)) {
                walkRingsThatShow(w);
                for (const auto* arrow : {&kDown, &kUp, &kLeft, &kRight}) {
                    auto* f = juce::Component::getCurrentlyFocusedComponent();
                    if (f == nullptr || !press(w, *f, *arrow))
                        break;
                }
            }
            keepSceneIntact();
        }
    }

    // A track row and a mixer column take their own keys (M, S, R, colour, fader, pan, hide, Return and the section
    // keys): the controls inside one are reached by those, as KeyboardReachTests.cpp's adapters say.
    static bool takesItsOwnKeys(juce::Component& c) {
        for (auto* p = c.getParentComponent(); p != nullptr; p = p->getParentComponent())
            if (dynamic_cast<synth::ui::TimelineTrackHeaderComponent*>(p) != nullptr)
                return true;
        return insideMixerColumn(c);
    }

    void checkReached(Walk& w, size_t& stopCount) {
        const auto* region = registry().findById(w.id);
        if (region == nullptr)
            return;
        std::set<juce::Component*> stops;
        for (auto* c : synth::ui::shownFocusableDescendants(*region->root))
            if (inRegion(c, w.id))
                stops.insert(c);
        for (auto& src : ringSourcesOf(*region))
            for (auto* c : src.stops())
                stops.insert(c);
        stopCount = stops.size();
        for (auto* c : stops)
            if (w.reached.count(c) == 0 && !takesItsOwnKeys(*c))
                add(w.id.toStdString(), describe(*c), "not reached by keys", {});
    }

    void walkRegion(const juce::String& id) {
        Walk w;
        w.id = id;
        registry().focusRegionById(id);
        synth::test::flushMessageQueue();
        judge(w, {"focus region", {}}, "(registry)", nullptr);
        enqueueLanding(w);
        walkRingsThatShow(w);
        auto* first = juce::Component::getCurrentlyFocusedComponent();
        const bool landedInside = first != nullptr && inRegion(first, id);
        explore(w);
        if (landedInside)
            globalKeysFrom(w);
        keepSceneIntact();
        size_t stopCount = 0;
        checkReached(w, stopCount);
        std::cout << "[ reach ] " << scene_ << " / " << id << ": " << stopCount << " stops, reached "
                  << w.reached.size() << ", " << w.keys << " keys\n";
    }

    MainComponent& mc_;
    juce::Component& helper_;
    SweepFindings& findings_;
    std::string scene_;
    std::function<void()> restoreScene_;
    std::string shape_;
    std::map<juce::Component*, bool> drawn_;
    int totalKeys_ = 0;
    int restores_ = 0;
    int unjudgedBounds_ = 0;
};

} // namespace synth::test::reach
