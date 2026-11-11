// TabOrderReadingSweepTests.cpp -- Tab visits the siblings of every focus region and every dialog in reading order.
// juce::KeyboardFocusTraverser orders the children of one parent by top edge, then left edge, so a row that mixes
// control heights is walked out of reading order (a full-height button before the short bar beside it). The sweep
// takes every parent in a surface, asks its traverser for the Tab order, and checks that the parent's children (each
// standing for the stops inside it) follow reading order: by row band, then left to right. The regions are discovered
// from a real MainComponent's FocusRegionRegistry and the dialogs from the theme sweep registry, as in
// KeyboardReachTests.cpp. A gap that is known and ticketed lives in kKnownGaps; the list may only shrink
// (docs/development/accessibility.md, "Tab order follows reading order").
#include "../Theme/ThemeSweep/ThemeSweepSurfaces.h"
#include "KeyboardReachFixture.h"
#include "UI/Theme/BuiltInThemes.h"
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <typeinfo>
#if defined(__GNUG__)
#include <cxxabi.h>
#endif

using synth::test::theme_sweep::dialogSurfaces;
using synth::test::theme_sweep::SweepEnvironment;
using Tab = synth::ui::BottomDockComponent::Tab;

namespace {

// A gap that is known and has a ticket. Key: the finding as the failure message prints it.
struct KnownGap {
    const char* key;
    const char* ticket;
};

const std::vector<KnownGap> kKnownGaps = {
    // By design the chat is input first (the prompt field, Send, Cancel, then the messages); New Chat, at the top,
    // comes last.
    {"aiPanel / AIChatComponent: Chat messages before New Chat", "DELIBERATE"},
    // By design the chat is input first (the prompt field, Send, Cancel, then the messages).
    {"aiPanel / AIChatComponent: Send before Chat messages", "DELIBERATE"},
    // By design the chat is input first (the prompt field, Send, Cancel, then the messages); New Chat, at the top,
    // comes last.
    {"aiPanel [piano roll open] / AIChatComponent: Chat messages before New Chat", "DELIBERATE"},
    // By design the chat is input first (the prompt field, Send, Cancel, then the messages).
    {"aiPanel [piano roll open] / AIChatComponent: Send before Chat messages", "DELIBERATE"},
    // By design the chat is input first (the prompt field, Send, Cancel, then the messages); New Chat, at the top,
    // comes last.
    {"aiPanel [drum machine open] / AIChatComponent: Chat messages before New Chat", "DELIBERATE"},
    // By design the chat is input first (the prompt field, Send, Cancel, then the messages).
    {"aiPanel [drum machine open] / AIChatComponent: Send before Chat messages", "DELIBERATE"},
    // A 2D map: its dots and its cursor are points on a plane, with no reading order between them (the map owns an
    // explicit order).
    {"Shape editor / ShapeWindowMap: Dot 3 of 3, shape oo, position X 50%, Y 80% before Morph cursor, X 0.50, Y 0.50",
     "DELIBERATE"},
    // A 2D map: the X slider runs along the bottom and the Y slider up the left edge; X then Y is the natural order.
    {"Shape editor / ShapeWindowMap: Morph X, 0.50 before Morph Y, 0.50", "DELIBERATE"},
};

std::string tabClassName(const juce::Component& c) {
#if defined(__GNUG__)
    int status = 0;
    char* demangled = abi::__cxa_demangle(typeid(c).name(), nullptr, nullptr, &status);
    std::string name = status == 0 && demangled != nullptr ? demangled : typeid(c).name();
    std::free(demangled);
#else
    std::string name = typeid(c).name();
    for (const char* prefix : {"class ", "struct "})
        if (name.rfind(prefix, 0) == 0)
            name = name.substr(std::char_traits<char>::length(prefix));
#endif
    const auto colons = name.rfind("::");
    return colons == std::string::npos ? name : name.substr(colons + 2);
}

// The title, else a button's text, else the class name.
std::string stopName(const juce::Component& c) {
    if (c.getTitle().isNotEmpty())
        return c.getTitle().toStdString();
    if (auto* button = dynamic_cast<const juce::Button*>(&c); button != nullptr && button->getButtonText().isNotEmpty())
        return button->getButtonText().toStdString();
    return tabClassName(c);
}

// One child of a parent, standing for the Tab stops at or inside it.
struct Unit {
    juce::Component* child = nullptr;
    size_t firstIndex = 0;       // where Tab first reaches it
    juce::Rectangle<int> bounds; // the child's own bounds, in the parent's coordinates
    std::string name;            // its first stop
};

// Two rectangles are one row when their vertical extents overlap by at least half of the shorter one.
bool sameBand(const juce::Rectangle<int>& a, const juce::Rectangle<int>& b) {
    const int overlap = std::min(a.getBottom(), b.getBottom()) - std::max(a.getY(), b.getY());
    return overlap > 0 && overlap * 2 >= std::min(a.getHeight(), b.getHeight());
}

// True when `b` may follow `a`: in the same row it lies to the right (or at the same left edge), in another row it lies
// lower.
bool mayFollow(const Unit& a, const Unit& b) {
    constexpr int slack = 1;
    // Controls laid over each other (a handle on its bar, an inspector over a clipped canvas) are not in a row.
    if (a.bounds.intersects(b.bounds))
        return true;
    if (sameBand(a.bounds, b.bounds))
        return b.bounds.getX() >= a.bounds.getX() - slack;
    return b.bounds.getCentreY() >= a.bounds.getCentreY() - slack;
}

// The units of `node` in the order Tab walks them; empty when fewer than two children hold a stop.
std::vector<Unit> tabUnits(juce::Component& node) {
    std::vector<Unit> units;
    auto traverser = node.createKeyboardFocusTraverser();
    if (traverser == nullptr)
        return units;
    const auto all = traverser->getAllComponents(&node);
    std::map<juce::Component*, size_t> byChild;
    for (size_t i = 0; i < all.size(); ++i) {
        auto* stop = all[i];
        if (stop == &node || stop->getBounds().isEmpty())
            continue;
        auto* child = stop;
        while (child != nullptr && child->getParentComponent() != &node)
            child = child->getParentComponent();
        if (child == nullptr)
            continue; // a stop that is not under this node (a custom traverser's own pick)
        if (byChild.find(child) == byChild.end()) {
            byChild[child] = units.size();
            units.push_back({child, i, child->getBounds(), stopName(*stop)});
        }
    }
    return units;
}

struct SweepCounts {
    int parents = 0; // parents with two or more children holding a stop
    int pairs = 0;   // consecutive pairs of children compared
};

// Every out-of-order pair of siblings under `node`, as "<node class>: <a> before <b>"; `keep` decides whether a node
// belongs to the surface being judged (a nested region is judged under its own id).
void sweepNode(juce::Component& node, const std::function<bool(juce::Component&)>& keep, std::set<std::string>& out,
               SweepCounts& counts) {
    if (keep(node)) {
        const auto units = tabUnits(node);
        if (units.size() >= 2) {
            ++counts.parents;
            for (size_t i = 1; i < units.size(); ++i) {
                ++counts.pairs;
                if (!mayFollow(units[i - 1], units[i]))
                    out.insert(tabClassName(node) + ": " + units[i - 1].name + " before " + units[i].name);
            }
        }
    }
    for (auto* child : node.getChildren()) {
        // A control's own parts (a slider's text box, a combo box's label) are not laid out by the app.
        const bool part = dynamic_cast<juce::Slider*>(child) != nullptr ||
                          dynamic_cast<juce::ComboBox*>(child) != nullptr ||
                          dynamic_cast<juce::TextEditor*>(child) != nullptr;
        if (child->isVisible() && child->getAlpha() > 0.0f && !part)
            sweepNode(*child, keep, out, counts);
    }
}

class Findings {
public:
    void add(const std::string& surface, juce::Component& root, const std::function<bool(juce::Component&)>& keep) {
        std::set<std::string> found;
        SweepCounts counts;
        sweepNode(root, keep, found, counts);
        std::cout << "[ tab order ] " << surface << ": " << counts.parents << " rows or columns, " << counts.pairs
                  << " sibling pairs, " << found.size() << " out of reading order\n";
        walked_.insert(surface);
        pairs_ += counts.pairs;
        for (const auto& f : found)
            found_.insert(surface + " / " + f);
    }

    // An unlisted finding fails; a listed one that is now in order fails too, so the list only shrinks. A known gap on
    // a surface this run did not walk is not judged.
    void expectOnlyKnownGaps() const {
        EXPECT_GT(pairs_, 0) << "the sweep compared no siblings - it is not seeing these surfaces";
        std::set<std::string> listed;
        for (const auto& gap : kKnownGaps) {
            const std::string key = gap.key;
            listed.insert(key);
            if (walked_.count(key.substr(0, key.find(" / "))) != 0 && found_.count(key) == 0)
                ADD_FAILURE() << "fixed - remove it from kKnownGaps: " << key << " (" << gap.ticket << ")";
        }
        for (const auto& key : found_)
            if (listed.count(key) == 0)
                ADD_FAILURE() << "Tab leaves reading order: " << key;
    }

private:
    std::set<std::string> walked_;
    std::set<std::string> found_;
    int pairs_ = 0;
};

class TabOrderSweepShell : public KeyboardReachFixture {
protected:
    // Each open region judges the controls it owns (the deepest region containing a parent).
    void sweepRegions(Findings& findings, std::set<std::string>& seen, const std::string& suffix) {
        const auto& registry = mc_->getFocusRegionsForTest();
        for (const auto& region : openRegions()) {
            const std::string name = (region.id + suffix).toStdString();
            if (!seen.insert(name).second)
                continue;
            const auto id = region.id;
            findings.add(name, *region.root, [&registry, id](juce::Component& c) {
                const auto* owner = registry.regionContaining(&c);
                return owner != nullptr && owner->id == id;
            });
        }
    }
};

class TabOrderSweepDialogs : public KeyboardReachDialogFixture {};

} // namespace

// The sweep's own rule, on a mod row's shape: a full-height button right of a short bar.
TEST(TabOrderReadingRule, ATallControlBesideAShortOneIsOutOfOrderUntilTheOrderIsSet) {
    juce::Component row;
    row.setSize(300, 40);
    juce::TextButton tall("Timeline");
    juce::Slider bar;
    bar.setTitle("Amount");
    tall.setWantsKeyboardFocus(true);
    bar.setWantsKeyboardFocus(true);
    row.addAndMakeVisible(tall);
    row.addAndMakeVisible(bar);
    tall.setBounds(200, 0, 40, 40);
    bar.setBounds(0, 10, 150, 10);

    std::set<std::string> found;
    SweepCounts counts;
    const auto all = [](juce::Component&) { return true; };
    sweepNode(row, all, found, counts);
    EXPECT_EQ(found, std::set<std::string>({"Component: Timeline before Amount"}));
    EXPECT_EQ(counts.pairs, 1);

    bar.setExplicitFocusOrder(1);
    tall.setExplicitFocusOrder(2);
    found.clear();
    sweepNode(row, all, found, counts);
    EXPECT_TRUE(found.empty()) << "an explicit focus order in reading order is accepted";
}

TEST(TabOrderReadingRule, RowsOfOneHeightAndStackedRowsAreInOrder) {
    juce::Component panel;
    panel.setSize(300, 100);
    juce::TextButton a("A"), b("B"), c("C"), d("D");
    int x = 0;
    for (auto* button : {&a, &b}) {
        button->setWantsKeyboardFocus(true);
        panel.addAndMakeVisible(*button);
        button->setBounds(x, 0, 50, 20);
        x += 60;
    }
    c.setWantsKeyboardFocus(true);
    d.setWantsKeyboardFocus(true);
    panel.addAndMakeVisible(c);
    panel.addAndMakeVisible(d);
    c.setBounds(0, 30, 50, 20);
    d.setBounds(60, 32, 50, 16);
    std::set<std::string> found;
    SweepCounts counts;
    sweepNode(panel, [](juce::Component&) { return true; }, found, counts);
    EXPECT_TRUE(found.empty());
    EXPECT_EQ(counts.pairs, 3);
}

TEST_F(TabOrderSweepShell, EveryMainViewRegionTabsInReadingOrder) {
    Findings findings;
    std::set<std::string> seen;
    // The routing pane is a region only while the side pane is open, and shows the selected track.
    const auto firstTrack = mc_->getTimelineDoc().getTracks().front().id;
    mc_->getTimelinePanel().selectTracks({firstTrack}, firstTrack);
    mc_->getTimelinePanel().toggleSidePane(true);
    mc_->simulateToggleModMatrixClick();
    for (const Tab view : {Tab::Canvas, Tab::Timeline, Tab::Mixer, Tab::MidiRemote}) {
        mc_->setMainViewForTest(view); // false when `view` is already there
        sweepRegions(findings, seen, "");
    }
    findings.expectOnlyKnownGaps();
}

TEST_F(TabOrderSweepShell, ThePianoRollRegionsTabInReadingOrder) {
    auto& panel = mc_->getTimelinePanel();
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    const auto clip = mc_->getTimelineDoc().addClip(mc_->getTimelineDoc().getTracks().front().id, 0.0, 8.0, "Clip");
    Findings findings;
    std::set<std::string> seen;
    panel.openPianoRoll(clip);
    panel.getPianoRoll().toggleScalePanel();
    mc_->resized();
    sweepRegions(findings, seen, " [piano roll open]");
    findings.expectOnlyKnownGaps();
}

TEST_F(TabOrderSweepShell, TheDrumMachineRegionsTabInReadingOrder) {
    auto& panel = mc_->getTimelinePanel();
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    const auto clip = mc_->getTimelineDoc().addClip(mc_->getTimelineDoc().getTracks().front().id, 0.0, 8.0, "Clip");
    Findings findings;
    std::set<std::string> seen;
    panel.openDrumMachine(clip);
    mc_->resized();
    sweepRegions(findings, seen, " [drum machine open]");
    findings.expectOnlyKnownGaps();
}

TEST_F(TabOrderSweepDialogs, EveryDialogTabsInReadingOrder) {
    Findings findings;
    const auto theme = synth::theme::builtInThemes().front();
    for (const auto& surface : dialogSurfaces()) {
        if (!surface.open)
            continue;
        SweepEnvironment env(theme);
        auto* window = surface.open(env);
        ASSERT_NE(window, nullptr) << surface.name;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
        findings.add(surface.name, *window, [](juce::Component&) { return true; });
    }
    findings.expectOnlyKnownGaps();
}
