// TimelineTrackHeaderContextMenuTests.cpp
//
// FRO60: the header context menu (Delete Track, Make Channel) has to be reachable from a REAL
// right-click anywhere on the row, not just on its own background pixels. TimelineTrackHeaderTests.cpp
// already covers buildContextMenu()/applyContextMenuChoice() and even a real right-click via
// header.mouseDown(...) directly — but that is EXACTLY the blind spot FRO25's live check found:
// calling header.mouseDown(...) proves nothing about whether a right-click that actually lands on a
// CHILD component (the name label, the M/S/R/A toggles) ever reaches mouseDown() at all, since JUCE
// hands a click to whichever component is directly under the cursor and never bubbles it to an
// ancestor on its own. Every test below drives the click through the CHILD's own mouseDown() with
// itself as the event's origin — the same shape real clicks take (Component.cpp's internalMouseDown
// calls `mouseDown(me)` on the deepest hit-tested component) — never the header's.
//
// Split into its own topic file rather than grown into TimelineTrackHeaderTests.cpp (912 lines,
// against this repo's 1000-line file cap — see root CLAUDE.md's Code structure section) and
// registered in Tests/CMakeLists.txt next to it.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;
using synth::ui::TimelineTrackHeaderComponent;
using synth::ui::TrackHeaderHost;

namespace {

// A minimal TrackHeaderHost stub — this file only cares whether a right-click reaches the header's
// context menu (and, for the regression checks, whether it does NOT also fire a child's own
// click), so it only needs to implement the pure virtuals plus deleteTrack/canMakeChannelForTrack/
// makeChannelForTrack. Kept file-local rather than shared with TimelineTrackHeaderTests.cpp's own
// StubTrackHeaderHost, matching this repo's existing per-test-file fixture convention (see e.g.
// ChannelFlowTestFixture.h vs MacroPortWidgetTests.cpp each keeping their own MouseEvent/menu
// helpers rather than a shared header).
class StubHost : public TrackHeaderHost {
public:
    std::vector<BindingOption> getAvailableTrackInNodes(TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(TrackId) override { ++deleteCalls; }
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }

    bool canMakeChannelForTrack(TrackId) const override { return canMakeChannel; }
    void makeChannelForTrack(TrackId) override { ++makeChannelCalls; }

    int deleteCalls = 0;
    int makeChannelCalls = 0;
    bool canMakeChannel = true;
};

// A doc + one track + a header wired to the stub host above.
struct HeaderFixture {
    explicit HeaderFixture() {
        trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        header = std::make_unique<TimelineTrackHeaderComponent>(doc, trackId, &host);
        header->setSize(160, TimelineTrackHeaderComponent::kRowHeight);
    }

    const synth::Track* track() const { return doc.getTrack(trackId); }

    TimelineDoc doc;
    TrackId trackId;
    StubHost host;
    std::unique_ptr<TimelineTrackHeaderComponent> header;
};

// Hand-built MouseEvent with `eventComponent`/`originator` set to the CHILD itself — same shape as
// TimelineTrackHeaderTests.cpp's own makeRowMouseEvent()/ChannelFlowTestFixture.h's
// realMouseEventCFT() (each test file keeps its own copy of this idiom; there is no OS event queue
// in a headless test binary). Calling `child.mouseDown(realChildMouseEvent(child, ...))` is what
// makes this the REAL path: it invokes the CHILD's own overridden mouseDown(), exactly what JUCE's
// internalMouseDown() does for a physical click landing on that child — never the header's.
juce::MouseEvent realChildMouseEvent(juce::Component& child, juce::ModifierKeys mods) {
    const auto pos = child.getLocalBounds().getCentre().toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &child, &child, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

juce::ModifierKeys rightClickMods() { return juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier); }

// Right-clicks `child` through its own real mouseDown() and captures whatever PopupMenu the header
// builds via its existing setShowContextMenuHookForTest() seam (buildContextMenu() itself never runs
// a real juce::PopupMenu, which cannot run headlessly — see TimelineTrackHeaderComponent.h).
juce::PopupMenu rightClickChild(TimelineTrackHeaderComponent& header, juce::Component& child) {
    juce::PopupMenu captured;
    header.setShowContextMenuHookForTest([&captured](juce::PopupMenu& menu) { captured = menu; });
    child.mouseDown(realChildMouseEvent(child, rightClickMods()));
    header.setShowContextMenuHookForTest(nullptr);
    return captured;
}

const juce::PopupMenu::Item* findItemByText(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text == text)
            return &it.getItem();
    return nullptr;
}

} // namespace

// =============================================================================
// A real right-click on the name label reaches the header's context menu.
// =============================================================================

TEST(TimelineTrackHeaderContextMenuTest, RightClickOnNameLabelOpensHeaderMenu) {
    HeaderFixture f;
    const auto menu = rightClickChild(*f.header, f.header->getNameLabel());

    EXPECT_NE(findItemByText(menu, "Delete Track"), nullptr)
        << "a right-click on the name label must reach the SAME menu a right-click on the row's own "
           "background does";
    EXPECT_NE(findItemByText(menu, "Make Channel"), nullptr);
}

TEST(TimelineTrackHeaderContextMenuTest, RightClickOnNameLabelChoosingDeleteTrackAsksTheHost) {
    HeaderFixture f;
    const auto menu = rightClickChild(*f.header, f.header->getNameLabel());
    const auto* item = findItemByText(menu, "Delete Track");
    ASSERT_NE(item, nullptr);

    f.header->applyContextMenuChoice(item->itemID);
    EXPECT_EQ(f.host.deleteCalls, 1);
}

// =============================================================================
// A real right-click on M/S/R/A reaches the SAME menu — and does not ALSO fire the button's own
// click (juce::Button, unlike juce::Slider, does not itself guard e.mods.isPopupMenu()).
// =============================================================================

TEST(TimelineTrackHeaderContextMenuTest, RightClickOnMuteButtonOpensHeaderMenuWithoutTogglingMute) {
    HeaderFixture f;
    ASSERT_FALSE(f.track()->muted);

    const auto menu = rightClickChild(*f.header, f.header->getMuteButton());

    EXPECT_NE(findItemByText(menu, "Delete Track"), nullptr);
    EXPECT_FALSE(f.track()->muted) << "a right-click must not ALSO fire the button's own onClick (toggle mute)";
}

TEST(TimelineTrackHeaderContextMenuTest, RightClickOnSoloButtonOpensHeaderMenuWithoutTogglingSolo) {
    HeaderFixture f;
    ASSERT_FALSE(f.track()->soloed);

    const auto menu = rightClickChild(*f.header, f.header->getSoloButton());

    EXPECT_NE(findItemByText(menu, "Delete Track"), nullptr);
    EXPECT_FALSE(f.track()->soloed) << "a right-click must not ALSO fire the button's own onClick (toggle solo)";
}

TEST(TimelineTrackHeaderContextMenuTest, RightClickOnArmButtonOpensHeaderMenuWithoutTogglingArm) {
    HeaderFixture f;
    ASSERT_FALSE(f.track()->armed);

    const auto menu = rightClickChild(*f.header, f.header->getArmButton());

    EXPECT_NE(findItemByText(menu, "Delete Track"), nullptr);
    EXPECT_FALSE(f.track()->armed) << "a right-click must not ALSO fire the button's own onClick (toggle arm)";
}

TEST(TimelineTrackHeaderContextMenuTest, RightClickOnAutomationButtonOpensHeaderMenuWithoutToggling) {
    HeaderFixture f;
    bool toggled = false;
    f.header->onAutomationToggleRequested = [&toggled](TrackId) { toggled = true; };

    const auto menu = rightClickChild(*f.header, f.header->getAutomationButton());

    EXPECT_NE(findItemByText(menu, "Delete Track"), nullptr);
    EXPECT_FALSE(toggled) << "a right-click must not ALSO fire onAutomationToggleRequested";
}

// =============================================================================
// Children with their OWN popup menu are untouched by this fix — the binding chip keeps opening
// the Track In menu, the colour swatch keeps opening the colour picker, on a right-click exactly
// as before. Proven here by showing the header's context-menu hook is never invoked for either.
// =============================================================================

TEST(TimelineTrackHeaderContextMenuTest, BindingChipRightClickNeverReachesTheHeaderMenu) {
    HeaderFixture f;
    bool hookCalled = false;
    f.header->setShowContextMenuHookForTest([&hookCalled](juce::PopupMenu&) { hookCalled = true; });

    // juce::Button re-declares mouseDown()/mouseUp() as protected (its own popup-menu-agnostic
    // click handling is internal), so the call has to go through the juce::Component& base — same
    // as rightClickChild() above does for its own `child` parameter.
    juce::Component& chip = f.header->getBindingChip();
    chip.mouseDown(realChildMouseEvent(chip, rightClickMods()));

    f.header->setShowContextMenuHookForTest(nullptr);
    EXPECT_FALSE(hookCalled) << "the binding chip keeps its OWN right-click menu (Track In) unchanged";
}

TEST(TimelineTrackHeaderContextMenuTest, ColourSwatchRightClickNeverReachesTheHeaderMenu) {
    HeaderFixture f;
    bool hookCalled = false;
    f.header->setShowContextMenuHookForTest([&hookCalled](juce::PopupMenu&) { hookCalled = true; });

    juce::Component& swatch = f.header->getColourSwatch();
    swatch.mouseDown(realChildMouseEvent(swatch, rightClickMods()));

    f.header->setShowContextMenuHookForTest(nullptr);
    EXPECT_FALSE(hookCalled) << "the colour swatch keeps its OWN right-click picker unchanged";
}
