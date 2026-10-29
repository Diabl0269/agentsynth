// KeyboardReachTests.cpp -- every control in every keyboard focus region, and in every dialog, is a keyboard stop.
// The regions are discovered from a real MainComponent's FocusRegionRegistry (each main view in turn, a piano roll and
// a drum machine open on a clip), the dialogs from the theme sweep registry, so a new region or dialog is walked
// without being listed here. A region that reaches some controls with keys of its own is described by an adapter below.
// A gap that is known and ticketed lives in kKnownGaps; the list may only shrink (docs/development/accessibility.md).
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestSettingsHelpers.h"
#include "../Theme/ThemeSweep/ThemeSweepSurfaces.h"
#include "BottomDockActiveTabResetGuard.h"
#include "NativeWindowTestGuard.h"
#include "UI/Chrome/CanvasToolbar/CanvasToolbar.h"
#include "UI/Chrome/ToolbarComponent.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Layout/LaneChips/LaneChipRow.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <cxxabi.h>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <typeinfo>

using synth::test::theme_sweep::dialogSurfaces;
using synth::test::theme_sweep::SweepEnvironment;
using Tab = synth::ui::BottomDockComponent::Tab;

namespace {

// A gap that is known and has a ticket. Key: "<region id or dialog name> / <control>" as the failure message prints it.
struct KnownGap {
    const char* key;
    const char* ticket;
};

const std::vector<KnownGap> kKnownGaps = {
    {"mixer / Audio 1 colour", "FRO1206"},
    {"timelineRouting / timelineChannelChip", "FRO1311"},
    {"mixer / Show Gate on canvas", "FRO1203"},
    {"mixer / Gate bypass, on", "FRO1203"},
    {"mixer / Show Parametric EQ on canvas", "FRO1203"},
    {"mixer / Parametric EQ bypass, on", "FRO1203"},
    {"mixer / Show Compressor on canvas", "FRO1203"},
    {"mixer / Compressor bypass, on", "FRO1203"},
    {"mixer / Add Bus", "FRO1292"},
    {"mixer / Reset Meters", "FRO1292"},
    {"mixer / Side pane", "FRO1292"},
    {"mixer / Filter channels", "FRO1292"},
    {"mixer / All channels", "FRO1292"},
    {"mixer / Tracks channels", "FRO1292"},
    {"mixer / Buses channels", "FRO1292"},
    {"mixer / Audio 1 pan", "FRO1310"},
    {"mixer / Audio 1 visibility", "FRO1310"},
    {"mixer / Direct visibility", "FRO1310"},
    {"mixer / Mixer pan law (this project): Compensated", "FRO1292"},
};

// A region that reaches some of its controls with keys of its own. `reaches` answers for one control; `reason` says
// how, in one line. A region with no adapter is held to the general rule: the control is one of
// synth::ui::focusableDescendants(region root).
struct Adapter {
    const char* regionId;
    const char* reason;
    // True when the root takes every key itself, so a child that wants keyboard focus is still not reachable (Tab is
    // the app's region cycle): only `reaches` decides.
    bool ownKeysOnly;
    // Controls the region owns that are not its descendants (siblings laid over its bounds); null when there are none.
    std::vector<juce::Component*> (*extraControls)(juce::Component& regionRoot);
    bool (*reaches)(juce::Component& control, juce::Component& regionRoot);
};

// The visible, enabled buttons that are siblings of `root` and sit inside its bounds.
std::vector<juce::Component*> siblingButtonsOver(juce::Component& root) {
    std::vector<juce::Component*> found;
    if (auto* parent = root.getParentComponent())
        for (auto* child : parent->getChildren())
            if (dynamic_cast<juce::Button*>(child) != nullptr && child->isVisible() && child->isEnabled() &&
                root.getBounds().contains(child->getBounds().getCentre()))
                found.push_back(child);
    return found;
}

// Home, then Right until the ring stops, reading the name of the button it sits on at each step.
template <typename Strip>
bool ringReaches(juce::Component& control, juce::Component& root) {
    auto& strip = static_cast<Strip&>(root);
    const auto press = [&](int code) { return strip.keyPressed(juce::KeyPress(code)); };
    const juce::String name =
        control.getTitle().isNotEmpty() ? control.getTitle() : static_cast<juce::Button&>(control).getButtonText();
    press(juce::KeyPress::homeKey);
    for (int guard = 0; guard < 40; ++guard) {
        if (strip.getFocusedButtonName() == name)
            return true;
        const int before = strip.getFocusedSlot();
        press(juce::KeyPress::rightKey);
        if (strip.getFocusedSlot() == before)
            break;
    }
    return strip.getFocusedButtonName() == name;
}

// The timeline's top bar is a key ring entered with Up from the track column; its stops are the ring.
bool timelineReaches(juce::Component& control, juce::Component& root) {
    if (dynamic_cast<synth::ui::TimelineTrackHeaderComponent*>(control.getParentComponent()) != nullptr)
        return true; // M, S and R on a focused track row
    const auto stops = static_cast<synth::ui::TimelinePanelComponent&>(root).getTopBarKeysForTest().getStops();
    return std::find(stops.begin(), stops.end(), &control) != stops.end();
}

// The mixer panel is one focusable leaf: Left/Right pick a column, Up/Down nudge its fader, M and S mute and solo it,
// Return selects it on the canvas and the section keys toggle the Inserts, Sends and EQ rows. Nothing else has a key.
bool mixerKeysReach(juce::Component& control, juce::Component&) {
    const auto title = control.getTitle();
    if (title == "Inserts" || title == "Sends" || title == "EQ")
        return true;
    return title.endsWith(" fader") || title.contains(" mute") || title.contains(" solo") ||
           (title.startsWith("Edit ") && title.endsWith(" on canvas"));
}

const std::vector<Adapter> kAdapters = {
    {"toolbar", "buttons are siblings of the root; Left/Right/Home/End move a ring over them", false,
     siblingButtonsOver, ringReaches<ToolbarComponent>},
    {"canvasToolbar", "Left/Right/Home/End move a ring over the buttons", false, nullptr,
     ringReaches<synth::ui::CanvasToolbar>},
    {"timeline",
     "the top bar is a key ring (Up from the track column, Left/Right along it); M, S and R are keys of a track row",
     false, nullptr, timelineReaches},
    {"mixer", "one focusable leaf: column, fader, mute, solo, select and section keys only", true, nullptr,
     mixerKeysReach},
    {"modMatrix", "a painted grid: the arrow keys move its cell cursor", false, nullptr, nullptr},
    {"dockTabs", "the tabs are painted: Left/Right/Home/End select one; Shift/Option+Return press its icons", false,
     nullptr, nullptr},
    {"Settings", "the tab strip is one stop; Left/Right/Home/End switch tabs", false, nullptr,
     [](juce::Component& control, juce::Component&) { return dynamic_cast<juce::TabBarButton*>(&control) != nullptr; }},
};

struct Control {
    juce::Component* component;
    std::string name;
};

std::string className(const juce::Component& c) {
    int status = 0;
    char* demangled = abi::__cxa_demangle(typeid(c).name(), nullptr, nullptr, &status);
    std::string name = status == 0 && demangled != nullptr ? demangled : typeid(c).name();
    std::free(demangled);
    const auto colons = name.rfind("::");
    return colons == std::string::npos ? name : name.substr(colons + 2);
}

// The title, else a button's text, else the class name.
std::string controlName(juce::Component& c) {
    if (c.getTitle().isNotEmpty())
        return c.getTitle().toStdString();
    if (auto* button = dynamic_cast<juce::Button*>(&c); button != nullptr && button->getButtonText().isNotEmpty())
        return button->getButtonText().toStdString();
    return className(c);
}

bool isOperable(juce::Component& c) {
    // A segmented switch and a lane chip row are each one control: it is the stop, and the arrow keys move between
    // its parts.
    return dynamic_cast<juce::Button*>(&c) != nullptr || dynamic_cast<juce::Slider*>(&c) != nullptr ||
           dynamic_cast<synth::ui::CardSegmentedSwitch*>(&c) != nullptr ||
           dynamic_cast<synth::ui::LaneChipRow*>(&c) != nullptr || dynamic_cast<juce::ComboBox*>(&c) != nullptr ||
           dynamic_cast<juce::TextEditor*>(&c) != nullptr;
}

bool isOnScreenInside(juce::Component& c, const juce::Component& root) {
    for (auto* p = &c; p != nullptr; p = p->getParentComponent()) {
        if (!p->isVisible() || p->getAlpha() <= 0.0f || p->getWidth() <= 0 || p->getHeight() <= 0)
            return false;
        if (p == &root)
            return true;
    }
    return false;
}

// Every visible, enabled button, slider, combo box and text editor under `root`. A control's own parts (a slider's text
// box, a combo box's label) are not looked into.
void collectOperable(juce::Component& root, juce::Component& node, std::vector<Control>& out) {
    for (auto* child : node.getChildren()) {
        if (!isOnScreenInside(*child, root))
            continue;
        if (isOperable(*child)) {
            if (child->isEnabled())
                out.push_back({child, controlName(*child)});
            continue;
        }
        collectOperable(root, *child, out);
    }
}

struct RegionReport {
    std::string name;
    int controls = 0;
    int stops = 0;
    std::string adapterReason;
    std::set<std::string> unreachable;
};

const Adapter* adapterFor(const std::string& id) {
    for (const auto& a : kAdapters)
        if (id == a.regionId)
            return &a;
    return nullptr;
}

// `owns` decides whether a control belongs to this surface (a nested region's controls are judged under that region).
RegionReport walkSurface(const std::string& name, juce::Component& root, const std::string& adapterId,
                         const std::function<bool(juce::Component&)>& owns) {
    RegionReport report;
    report.name = name;
    std::vector<Control> controls;
    collectOperable(root, root, controls);
    const auto* adapter = adapterFor(adapterId);
    const size_t ownControls = controls.size();
    if (adapter != nullptr && adapter->extraControls != nullptr)
        for (auto* extra : adapter->extraControls(root))
            controls.push_back({extra, controlName(*extra)});
    const auto stops = synth::ui::focusableDescendants(root);
    if (adapter != nullptr)
        report.adapterReason = adapter->reason;
    report.stops = (int)stops.size();
    for (size_t i = 0; i < controls.size(); ++i) {
        const auto& control = controls[i];
        if (i < ownControls && !owns(*control.component))
            continue;
        ++report.controls;
        const bool isStop = std::find(stops.begin(), stops.end(), control.component) != stops.end();
        const bool ownKeysOnly = adapter != nullptr && adapter->ownKeysOnly;
        const bool byOwnKeys =
            adapter != nullptr && adapter->reaches != nullptr && adapter->reaches(*control.component, root);
        if ((isStop && !ownKeysOnly) || byOwnKeys)
            continue;
        report.unreachable.insert(name + " / " + control.name);
    }
    return report;
}

void printReport(const RegionReport& r) {
    std::cout << "[ keyboard reach ] " << r.name << ": " << r.controls << " controls, " << r.stops << " stops, "
              << r.unreachable.size() << " unreachable"
              << (r.adapterReason.empty() ? "" : ", adapter: " + r.adapterReason) << "\n";
}

// Collects every report of a run, then compares the unreachable controls with kKnownGaps.
class Findings {
public:
    void add(const RegionReport& report) {
        printReport(report);
        walked_.insert(report.name);
        found_.insert(report.unreachable.begin(), report.unreachable.end());
        if (report.controls == 0 && report.adapterReason.empty())
            ADD_FAILURE() << report.name
                          << ": no operable controls and no adapter - the walk is not seeing this surface";
    }

    // An unlisted gap fails; a listed one that is now reachable fails too, so the list only shrinks. A known gap on a
    // surface this run did not walk is not judged.
    void expectOnlyKnownGaps() const {
        std::set<std::string> listed;
        for (const auto& gap : kKnownGaps) {
            const std::string key = gap.key;
            listed.insert(key);
            if (walked_.count(key.substr(0, key.find(" / "))) != 0 && found_.count(key) == 0)
                ADD_FAILURE() << "fixed - remove it from kKnownGaps: " << key << " (" << gap.ticket << ")";
        }
        for (const auto& key : found_)
            if (listed.count(key) == 0)
                ADD_FAILURE() << "not a keyboard stop: " << key;
    }

private:
    std::set<std::string> walked_;
    std::set<std::string> found_;
};

class KeyboardReachShell : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        resetGuard_.emplace();
        mc_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc_->setSize(1400, 900);
        mc_->newPatchForTest();
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false); // a hidden pane is hidden at once
        mc_->simulateAddAudioTrackClick();
        mc_->getTimelineDoc().addTrack(synth::TrackKind::Midi, "Track 1");
        mc_->getBottomDock().getMixerPanel().rebuild();
        mc_->resized();
    }
    void TearDown() override {
        mc_.reset();
        resetGuard_.reset();
        MainComponentTest::TearDown();
    }

    // Every region of the registry as it stands, opened first when it is closed and can be opened.
    std::vector<synth::ui::FocusRegion> openRegions() {
        std::vector<synth::ui::FocusRegion> regions;
        for (const auto& region : mc_->getFocusRegionsForTest().getRegions()) {
            if (!region.isCurrentlyOpen() && region.open)
                region.open();
            mc_->resized();
        }
        for (const auto& region : mc_->getFocusRegionsForTest().getRegions())
            if (region.isCurrentlyOpen() && region.root != nullptr)
                regions.push_back(region);
        return regions;
    }

    // A control belongs to the deepest region that contains it.
    void walkRegions(Findings& findings, std::set<std::string>& seen, const std::string& suffix) {
        const auto& registry = mc_->getFocusRegionsForTest();
        for (const auto& region : openRegions()) {
            const std::string name = (region.id + suffix).toStdString();
            if (!seen.insert(name).second)
                continue;
            const auto id = region.id;
            findings.add(walkSurface(name, *region.root, id.toStdString(), [&registry, id](juce::Component& c) {
                const auto* owner = registry.regionContaining(&c);
                return owner != nullptr && owner->id == id;
            }));
        }
    }

    std::optional<BottomDockActiveTabResetGuardMDT> resetGuard_;
    std::unique_ptr<MainComponent> mc_;
};

class KeyboardReachDialogs : public ::testing::Test {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        juce::MessageManager::getInstance();
        wasEnabled_ = synth::ui::PopupMotion::isEnabled();
        synth::ui::PopupMotion::setEnabled(false);
    }
    void TearDown() override {
        synth::ui::deleteOpenModalDialogs();
        synth::ui::PopupMotion::setEnabled(wasEnabled_);
    }
    bool wasEnabled_ = true;
};

} // namespace

TEST_F(KeyboardReachShell, EveryControlInEveryMainViewRegionIsAKeyboardStop) {
    Findings findings;
    std::set<std::string> seen;
    // The routing pane is a region only while the side pane is open, and shows the selected track.
    const auto firstTrack = mc_->getTimelineDoc().getTracks().front().id;
    mc_->getTimelinePanel().selectTracks({firstTrack}, firstTrack);
    mc_->getTimelinePanel().toggleSidePane(true);
    mc_->simulateToggleModMatrixClick();
    for (const Tab view : {Tab::Canvas, Tab::Timeline, Tab::Mixer, Tab::MidiRemote}) {
        mc_->setMainViewForTest(view); // false when `view` is already there
        walkRegions(findings, seen, "");
    }
    findings.expectOnlyKnownGaps();
}

TEST_F(KeyboardReachShell, EveryControlWithThePianoRollOpenIsAKeyboardStop) {
    auto& panel = mc_->getTimelinePanel();
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    const auto clip = mc_->getTimelineDoc().addClip(mc_->getTimelineDoc().getTracks().front().id, 0.0, 8.0, "Clip");
    Findings findings;
    std::set<std::string> seen;
    panel.openPianoRoll(clip);
    panel.getPianoRoll().toggleScalePanel();
    mc_->resized();
    walkRegions(findings, seen, " [piano roll open]");
    findings.expectOnlyKnownGaps();
}

TEST_F(KeyboardReachShell, EveryControlWithTheDrumMachineOpenIsAKeyboardStop) {
    auto& panel = mc_->getTimelinePanel();
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    const auto clip = mc_->getTimelineDoc().addClip(mc_->getTimelineDoc().getTracks().front().id, 0.0, 8.0, "Clip");
    Findings findings;
    std::set<std::string> seen;
    panel.openDrumMachine(clip);
    mc_->resized();
    walkRegions(findings, seen, " [drum machine open]");
    findings.expectOnlyKnownGaps();
}

TEST_F(KeyboardReachDialogs, EveryControlInEveryDialogIsAKeyboardStop) {
    Findings findings;
    const auto theme = synth::theme::builtInThemes().front();
    for (const auto& surface : dialogSurfaces()) {
        if (!surface.open)
            continue;
        SweepEnvironment env(theme);
        auto* window = surface.open(env);
        ASSERT_NE(window, nullptr) << surface.name;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
        findings.add(walkSurface(surface.name, *window, surface.name, [](juce::Component&) { return true; }));
    }
    findings.expectOnlyKnownGaps();
}
