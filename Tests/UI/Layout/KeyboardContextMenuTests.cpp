// KeyboardContextMenuTests.cpp -- opening a surface's right-click menu from the keyboard
// (KeyboardContextMenu.h, the "openContextMenu" action, Shift+F10 by default).
//
// Groups:
//   1. The resolver: nearest provider wins, none means unhandled.
//   2. The real key path: MainComponent::keyPressed -> command table -> resolver -> the focused
//      surface's own menu builder, for the timeline track header, the mixer and the library. Real
//      OS focus needs a native peer a headless run never has, so MainComponent's
//      setFocusedComponentOverrideForTest stands in for it; a juce::PopupMenu never opens headlessly,
//      so each surface's existing menu test seam captures the built menu instead.
//   3. Rebinding the action moves the behaviour.
//   4. The clip lane and the detached panel window.
#include "../../TestWait.h"
#include "../Mixer/MixerZonesTestRig.h"
#include "../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "../Timeline/TimelineClipLane/TimelineClipLaneTestFixture.h"
#include "Modules/Filter/CustomFilterShapes.h"
#include "Modules/Lfo/LfoCustomShapes.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Layout/CardCallOut.h"
#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include "UI/Layout/KeyboardContextMenu.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/MorphMap/MorphMapPalette.h"
#include "UI/ModuleViews/ShapeEditor/ShapeMapController.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ui::KeyboardContextMenuProvider;
using synth::ui::openContextMenuForFocusedComponent;

namespace {

juce::KeyPress shiftF10() { return juce::KeyPress(juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0); }
juce::KeyPress rightKey() { return juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers, 0); }

struct CountingProvider
    : juce::Component
    , KeyboardContextMenuProvider {
    explicit CountingProvider(bool answer)
        : answer_(answer) {}
    bool showContextMenuForKeyboardFocus() override {
        ++calls;
        return answer_;
    }
    int calls = 0;

private:
    bool answer_;
};

// Sends Shift+F10 the way a real press reaches the app: through MainComponent::keyPressed, whose
// command dispatch runs on the next message-loop pump.
bool pressShiftF10(MainComponent& mc, juce::Component* focused) {
    mc.setFocusedComponentOverrideForTest(focused);
    const bool handled = mc.keyPressed(shiftF10());
    synth::test::flushMessageQueue();
    return handled;
}

void hideWelcome(MainComponent& mc) {
    if (auto* ws = mc.getWelcomeScreenForTest())
        ws->setVisible(false);
}

std::vector<juce::String> itemTexts(juce::PopupMenu& menu) {
    std::vector<juce::String> texts;
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        texts.push_back(it.getItem().text);
    return texts;
}

bool runItem(juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next()) {
        if (it.getItem().text == text && it.getItem().action) {
            it.getItem().action();
            return true;
        }
    }
    return false;
}

} // namespace

// ============================================================================
// 1. Resolver
// ============================================================================

TEST(KeyboardContextMenuResolver, WalksUpToTheNearestProvider) {
    CountingProvider outer(true), inner(true);
    juce::Component leaf;
    outer.addAndMakeVisible(inner);
    inner.addAndMakeVisible(leaf);

    EXPECT_TRUE(openContextMenuForFocusedComponent(&leaf));
    EXPECT_EQ(inner.calls, 1);
    EXPECT_EQ(outer.calls, 0) << "only the nearest provider is asked";
}

TEST(KeyboardContextMenuResolver, TheFocusedComponentItselfMayBeTheProvider) {
    CountingProvider provider(true);
    EXPECT_TRUE(openContextMenuForFocusedComponent(&provider));
    EXPECT_EQ(provider.calls, 1);
}

TEST(KeyboardContextMenuResolver, AProviderThatDeclinesIsFinalAndNeverFallsThroughToAnOuterOne) {
    CountingProvider outer(true), inner(false);
    juce::Component leaf;
    outer.addAndMakeVisible(inner);
    inner.addAndMakeVisible(leaf);

    EXPECT_FALSE(openContextMenuForFocusedComponent(&leaf));
    EXPECT_EQ(inner.calls, 1);
    EXPECT_EQ(outer.calls, 0);
}

TEST(KeyboardContextMenuResolver, NoProviderOrNoFocusIsUnhandled) {
    juce::Component parent, leaf;
    parent.addAndMakeVisible(leaf);
    EXPECT_FALSE(openContextMenuForFocusedComponent(&leaf));
    EXPECT_FALSE(openContextMenuForFocusedComponent(nullptr));
}

// ============================================================================
// 2. The real key path
// ============================================================================

TEST(KeyboardContextMenuKeyPath, TheActionIsGeneralAndBoundToShiftF10ByDefault) {
    ShortcutManager manager;
    EXPECT_EQ(ShortcutManager::getCategory("openContextMenu"), ShortcutCategory::General);
    EXPECT_EQ(ShortcutManager::getActionDescription("openContextMenu"), "Open Context Menu");
    EXPECT_TRUE(ShortcutManager::keyPressMatches(manager.getBinding("openContextMenu"), shiftF10()));
    EXPECT_TRUE(manager.getConflictingAction("openContextMenu", manager.getBinding("openContextMenu")).isEmpty());
}

TEST(KeyboardContextMenuKeyPath, TrackHeaderOpensTheMenuOfTheFocusedTrackOnly) {
    MixerZonesRig r(2);
    hideWelcome(r.mc);
    auto* first = r.mc.getTimelinePanel().getTrackHeaderAt(0);
    auto* second = r.mc.getTimelinePanel().getTrackHeaderAt(1);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    int firstMenus = 0, secondMenus = 0;
    std::vector<juce::String> secondItems;
    first->setShowContextMenuHookForTest([&](juce::PopupMenu&) { ++firstMenus; });
    second->setShowContextMenuHookForTest([&](juce::PopupMenu& menu) {
        ++secondMenus;
        secondItems = itemTexts(menu);
    });

    pressShiftF10(r.mc, second);
    EXPECT_EQ(secondMenus, 1);
    EXPECT_EQ(firstMenus, 0) << "the menu is built for the focused track, not the first one";
    EXPECT_NE(std::find(secondItems.begin(), secondItems.end(), "Delete Track"), secondItems.end())
        << "the same builder a right-click uses";

    pressShiftF10(r.mc, first);
    EXPECT_EQ(firstMenus, 1);
    EXPECT_EQ(secondMenus, 1);
}

TEST(KeyboardContextMenuKeyPath, TrackHeaderMenuOpensFromAChildOfTheFocusedRow) {
    MixerZonesRig r(1);
    hideWelcome(r.mc);
    auto* header = r.mc.getTimelinePanel().getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    int menus = 0;
    header->setShowContextMenuHookForTest([&](juce::PopupMenu&) { ++menus; });

    pressShiftF10(r.mc, &header->getNameLabel());
    EXPECT_EQ(menus, 1);
}

TEST(KeyboardContextMenuKeyPath, MixerOpensTheHeaderMenuOfTheFocusedColumn) {
    MixerZonesRig r(2);
    hideWelcome(r.mc);
    const auto first = r.stripId(0);
    const auto second = r.stripId(1);
    ASSERT_TRUE(first.isNotEmpty() && second.isNotEmpty());

    // Nothing focused yet: no menu. (keyPressed reports the command as dispatched either way; the
    // provider's own answer is not observable through it.)
    int menus = 0;
    r.panel->setShowZoneMenuHookForTest([&](juce::PopupMenu&) { ++menus; });
    pressShiftF10(r.mc, r.panel);
    EXPECT_EQ(menus, 0);

    r.panel->keyPressed(rightKey()); // column 0
    r.panel->keyPressed(rightKey()); // column 1
    ASSERT_EQ(r.panel->getFocusedColumnIndexForTest(), 1);

    // Picking "Pin left" proves whose menu it was: the pin lands on the focused column's channel.
    r.hookMenuToPick("Pin left");
    pressShiftF10(r.mc, r.panel);
    EXPECT_EQ(r.panel->getViewDoc().getZone(second), synth::MixerZone::Left);
    EXPECT_NE(r.panel->getViewDoc().getZone(first), synth::MixerZone::Left);
}

TEST(KeyboardContextMenuKeyPath, LibraryOpensTheMenuOfTheFocusedSnippetRow) {
    MixerZonesRig r(0);
    hideWelcome(r.mc);
    auto& library = r.mc.getModuleLibrary();
    library.setSize(260, 900);
    synth::SnippetInfo a, b;
    a.name = "Alpha";
    b.name = "Beta";
    library.setSnippets({a, b});

    juce::String deleted;
    library.onSnippetDeleteRequested = [&](const juce::String& name) { deleted = name; };
    std::vector<juce::String> items;
    juce::PopupMenu captured;
    library.setShowContextMenuHookForTest([&](juce::PopupMenu& menu) {
        items = itemTexts(menu);
        captured = menu;
    });

    int betaRow = -1;
    for (int i = 0; i < library.getEntryCount(); ++i)
        if (library.getEntryText(i) == "Beta")
            betaRow = i;
    ASSERT_GE(betaRow, 0);
    library.setKeyboardFocusedIndexForTest(betaRow);

    pressShiftF10(r.mc, &library);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0], "Delete Snippet");
    ASSERT_TRUE(runItem(captured, "Delete Snippet"));
    EXPECT_EQ(deleted, "Beta") << "the menu is for the keyboard-focused row";
}

TEST(KeyboardContextMenuKeyPath, LibraryRowWithoutAMenuOpensNothing) {
    MixerZonesRig r(0);
    hideWelcome(r.mc);
    auto& library = r.mc.getModuleLibrary();
    library.setSize(260, 900);
    int menus = 0;
    library.setShowContextMenuHookForTest([&](juce::PopupMenu&) { ++menus; });

    library.setKeyboardFocusedIndexForTest(0); // a section header: no right-click menu
    pressShiftF10(r.mc, &library);
    EXPECT_EQ(menus, 0);
}

TEST(KeyboardContextMenuKeyPath, WelcomeScreenSuppressesTheAction) {
    MixerZonesRig r(1);
    auto* header = r.mc.getTimelinePanel().getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    int menus = 0;
    header->setShowContextMenuHookForTest([&](juce::PopupMenu&) { ++menus; });

    r.mc.getWelcomeScreenForTest()->setVisible(true);
    EXPECT_FALSE(pressShiftF10(r.mc, header)) << "an inactive command refuses the key";
    EXPECT_EQ(menus, 0);
}

// ============================================================================
// 3. Rebinding
// ============================================================================

TEST(KeyboardContextMenuRebind, RebindingMovesTheBehaviourToTheNewKey) {
    MixerZonesRig r(1);
    hideWelcome(r.mc);
    auto* header = r.mc.getTimelinePanel().getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    int menus = 0;
    header->setShowContextMenuHookForTest([&](juce::PopupMenu&) { ++menus; });

    const juce::KeyPress shiftF9(juce::KeyPress::F9Key, juce::ModifierKeys::shiftModifier, 0);
    r.mc.getShortcutManager().setBinding("openContextMenu", shiftF9);
    r.mc.setFocusedComponentOverrideForTest(header);

    EXPECT_FALSE(r.mc.keyPressed(shiftF10())) << "the old chord no longer opens anything";
    synth::test::flushMessageQueue();
    EXPECT_EQ(menus, 0);

    EXPECT_TRUE(r.mc.keyPressed(shiftF9));
    synth::test::waitUntil([&] { return (menus) == (1); });
    EXPECT_EQ(menus, 1);
}

// ============================================================================
// 4. Clip lane and detached window
// ============================================================================

namespace {
class MenuRecordingLane : public TimelineClipLaneArea {
public:
    using TimelineClipLaneArea::TimelineClipLaneArea;
    std::vector<std::pair<synth::ClipId, juce::Point<int>>> menus;

protected:
    void showClipContextMenu(synth::ClipId id, juce::Point<int> localPos) override { menus.emplace_back(id, localPos); }
};
} // namespace

TEST(KeyboardContextMenuClipLane, OpensTheSelectedClipsMenuAtTheClipsMiddle) {
    TimelineDoc doc;
    TimelineViewState state;
    ClipSelectionModel selection;
    AppUndoManager undo;
    MenuRecordingLane lane{state, selection};
    state.pixelsPerBeat = 40.0;
    lane.setTimelineDoc(&doc);
    lane.setUndoManager(&undo);
    lane.setSize(1200, 400);
    const auto track = doc.addTrack(TrackKind::Midi, "t");
    const auto clip = doc.addClip(track, 4.0, 8.0, "c");

    EXPECT_FALSE(openContextMenuForFocusedComponent(&lane)) << "nothing selected";
    EXPECT_TRUE(lane.menus.empty());

    selection.setSelection({clip});
    EXPECT_TRUE(openContextMenuForFocusedComponent(&lane));
    ASSERT_EQ(lane.menus.size(), 1u);
    EXPECT_EQ(lane.menus[0].first, clip);
    EXPECT_EQ(lane.menus[0].second, lane.getClipRect(clip).getCentre());
}

TEST(KeyboardContextMenuDetachedWindow, TheBoundKeyReachesTheFocusedSurfaceAndHonoursRebinds) {
    struct Panel : juce::Component {};
    Panel panel;
    juce::String title{"Test"};
    juce::ApplicationProperties appProperties;
    ShortcutManager shortcutManager;
    synth::ui::DetachedPanelWindow window(panel, title, "kcmWindowBounds", &appProperties, nullptr, &shortcutManager);

    CountingProvider provider(true);
    panel.addAndMakeVisible(provider);
    window.setFocusedComponentOverrideForTest(&provider);

    EXPECT_TRUE(window.keyPressed(shiftF10()));
    EXPECT_EQ(provider.calls, 1);

    const juce::KeyPress shiftF9(juce::KeyPress::F9Key, juce::ModifierKeys::shiftModifier, 0);
    shortcutManager.setBinding("openContextMenu", shiftF9);
    EXPECT_FALSE(window.keyPressed(shiftF10()));
    EXPECT_TRUE(window.keyPressed(shiftF9));
    EXPECT_EQ(provider.calls, 2);
}

// ============================================================================
// 5. Recolour Focused Item (Alt+C): the same provider walk for colour pickers
// ============================================================================
// Bug kinds: keyboard-reach (every "Colour..." menu item has a key) and shortcut-relay (the key also works in a
// detached window and a card call-out).
using synth::ui::ColourPickerPopup;
using synth::ui::KeyboardColourProvider;
using synth::ui::openColourForFocusedComponent;

namespace {

juce::KeyPress altC() { return juce::KeyPress('c', juce::ModifierKeys::altModifier, 0); }

struct ColourCountingProvider
    : juce::Component
    , KeyboardColourProvider {
    explicit ColourCountingProvider(bool answer)
        : answer_(answer) {}
    bool openColourForKeyboardFocus() override {
        ++calls;
        return answer_;
    }
    int calls = 0;

private:
    bool answer_;
};

bool pressAltC(MainComponent& mc, juce::Component* focused) {
    mc.setFocusedComponentOverrideForTest(focused);
    const bool handled = mc.keyPressed(altC());
    synth::test::flushMessageQueue();
    return handled;
}

// Takes the place of the card call-out window: keeps the content the key opened and where it was anchored.
struct CapturedCallOut {
    CapturedCallOut() {
        synth::ui::test_hooks::cardCallOutLauncherForTest() = [this](std::unique_ptr<juce::Component> c,
                                                                     juce::Rectangle<int> a) {
            ++opened;
            area = a;
            content = std::move(c);
            return nullptr;
        };
    }
    ~CapturedCallOut() { synth::ui::test_hooks::cardCallOutLauncherForTest() = nullptr; }
    ColourPickerPopup* picker() const { return dynamic_cast<ColourPickerPopup*>(content.get()); }
    int opened = 0;
    juce::Rectangle<int> area;
    std::unique_ptr<juce::Component> content;
};

} // namespace

TEST(KeyboardColourAction, IsGeneralBoundToAltCByDefaultAndConflictsWithNothing) {
    ShortcutManager manager;
    EXPECT_EQ(ShortcutManager::getCategory("recolourFocusedItem"), ShortcutCategory::General);
    EXPECT_EQ(ShortcutManager::getActionDescription("recolourFocusedItem"), "Recolour Focused Item");
    EXPECT_TRUE(ShortcutManager::keyPressMatches(manager.getBinding("recolourFocusedItem"), altC()));
    EXPECT_TRUE(
        manager.getConflictingAction("recolourFocusedItem", manager.getBinding("recolourFocusedItem")).isEmpty());
}

TEST(KeyboardColourResolver, NearestProviderAnswersAndADecliningOneIsFinal) {
    ColourCountingProvider outer(true), inner(false);
    juce::Component leaf;
    outer.addAndMakeVisible(inner);
    inner.addAndMakeVisible(leaf);
    EXPECT_FALSE(openColourForFocusedComponent(&leaf));
    EXPECT_EQ(inner.calls, 1);
    EXPECT_EQ(outer.calls, 0);
    EXPECT_FALSE(openColourForFocusedComponent(nullptr));
}

TEST(KeyboardColourKeyPath, AltCOpensTheFocusedTracksPickerAndAPickIsOneUndoStep) {
    MixerZonesRig r(2);
    hideWelcome(r.mc);
    auto* second = r.mc.getTimelinePanel().getTrackHeaderAt(1);
    ASSERT_NE(second, nullptr);
    CapturedCallOut out;

    EXPECT_TRUE(pressAltC(r.mc, second));
    ASSERT_EQ(out.opened, 1);
    ASSERT_NE(out.picker(), nullptr);
    EXPECT_EQ(out.area, second->getColourSwatch().getScreenBounds()) << "beside the swatch a click opens it from";

    const auto original = r.mc.getTimelineDoc().getTrack(second->getTrackId())->colourArgb;
    out.picker()->setCurrentColourForTest(juce::Colours::red);
    EXPECT_EQ(r.mc.getTimelineDoc().getTrack(second->getTrackId())->colourArgb, juce::Colours::red.getARGB());
    out.content.reset(); // the popup closes: the commit
    EXPECT_TRUE(r.mc.getUndoManager().undo());
    EXPECT_EQ(r.mc.getTimelineDoc().getTrack(second->getTrackId())->colourArgb, original);
}

TEST(KeyboardColourKeyPath, AltCOpensThePickerOfAChildOfTheFocusedRow) {
    MixerZonesRig r(1);
    hideWelcome(r.mc);
    auto* header = r.mc.getTimelinePanel().getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    CapturedCallOut out;
    EXPECT_TRUE(pressAltC(r.mc, &header->getNameLabel()));
    EXPECT_EQ(out.opened, 1);
}

TEST(KeyboardColourKeyPath, MixerOpensThePickerOfTheFocusedStripAndLeavesMasterUnhandled) {
    MixerZonesRig r(2);
    hideWelcome(r.mc);
    int opened = 0;
    r.panel->setShowColourPickerHookForTest(
        [&](auto, std::unique_ptr<ColourPickerPopup>, juce::Rectangle<int>) { ++opened; });
    const juce::KeyPress right(juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers, 0);

    EXPECT_FALSE(openColourForFocusedComponent(r.panel)) << "no column focused yet";
    r.panel->keyPressed(right);
    r.panel->keyPressed(right);
    EXPECT_TRUE(openColourForFocusedComponent(r.panel));
    EXPECT_EQ(opened, 1);
    for (int i = 0; i < r.panel->getColumnCount(); ++i)
        r.panel->keyPressed(right);
    EXPECT_FALSE(openColourForFocusedComponent(r.panel)) << "Master has no colour";
    EXPECT_EQ(opened, 1);
}

TEST(KeyboardColourKeyPath, WelcomeScreenSuppressesTheAction) {
    MixerZonesRig r(1);
    auto* header = r.mc.getTimelinePanel().getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    CapturedCallOut out;
    r.mc.getWelcomeScreenForTest()->setVisible(true);
    EXPECT_FALSE(pressAltC(r.mc, header));
    EXPECT_EQ(out.opened, 0);
}

TEST(KeyboardColourKeyPath, RebindingMovesTheBehaviourToTheNewKey) {
    MixerZonesRig r(1);
    hideWelcome(r.mc);
    auto* header = r.mc.getTimelinePanel().getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    CapturedCallOut out;
    const juce::KeyPress altJ('j', juce::ModifierKeys::altModifier, 0);
    r.mc.getShortcutManager().setBinding("recolourFocusedItem", altJ);
    r.mc.setFocusedComponentOverrideForTest(header);
    EXPECT_FALSE(r.mc.keyPressed(altC()));
    synth::test::flushMessageQueue();
    EXPECT_EQ(out.opened, 0);
    EXPECT_TRUE(r.mc.keyPressed(altJ));
    synth::test::waitUntil([&] { return out.opened == 1; });
    EXPECT_EQ(out.opened, 1);
}

TEST(KeyboardColourDetachedWindow, TheBoundKeyReachesTheFocusedSurface) {
    struct Panel : juce::Component {};
    Panel panel;
    juce::String title{"Test"};
    juce::ApplicationProperties appProperties;
    ShortcutManager shortcutManager;
    synth::ui::DetachedPanelWindow window(panel, title, "kcolWindowBounds", &appProperties, nullptr, &shortcutManager);
    ColourCountingProvider provider(true);
    panel.addAndMakeVisible(provider);
    window.setFocusedComponentOverrideForTest(&provider);

    EXPECT_TRUE(window.keyPressed(altC()));
    EXPECT_EQ(provider.calls, 1);
}

// ---------------------------------------------------------------------------------------------------------------
// The morph map dot
// ---------------------------------------------------------------------------------------------------------------

namespace colour_test {
using namespace synth::ui;
using namespace synth::ui::test;

struct DotRig : ShapeRig {
    MorphMap map;
    ShapeMapController controller{map, *adapter};
    CapturedMenu menu;
    CapturedCallOut out;

    DotRig() {
        map.setSize(252, 150);
        map.showMenu = [this](juce::PopupMenu& m, juce::Rectangle<int>) { menu.capture(m); };
        controller.refresh();
    }
    std::optional<juce::uint32> storedColour(size_t i) const { return filter->getCustomShapes().shapes[i].colour; }
};
} // namespace colour_test
using namespace colour_test;

TEST(MorphDotColour, TheDotMenuOffersColourAndItOpensThePickerBesideTheDot) {
    DotRig rig;
    rig.map.showDotMenu(1);
    ASSERT_NE(std::find(rig.menu.items.begin(), rig.menu.items.end(), "Colour..."), rig.menu.items.end());
    ASSERT_TRUE(rig.menu.run("Colour..."));
    ASSERT_EQ(rig.out.opened, 1);
    EXPECT_NE(rig.out.picker(), nullptr);
}

TEST(MorphDotColour, PreviewShowsOnTheDotLiveAndTheCommitIsExactlyOneUndoStep) {
    DotRig rig;
    ASSERT_FALSE(rig.storedColour(1).has_value());
    const int steps = rig.undo.getEditSerial();
    ASSERT_TRUE(rig.map.showDotColour(1));
    auto* picker = rig.out.picker();
    ASSERT_NE(picker, nullptr);

    for (const auto c : {juce::Colours::red, juce::Colours::green, juce::Colours::blue}) {
        picker->setCurrentColourForTest(c);
        EXPECT_EQ(rig.storedColour(1), c.getARGB()) << "the module wears each tick";
        EXPECT_EQ(rig.map.getDots()[1].colour, c.getARGB()) << "and so does the dot";
    }
    EXPECT_EQ(rig.undo.getEditSerial(), steps) << "dragging records nothing";

    rig.out.content.reset(); // the popup closes
    EXPECT_EQ(rig.storedColour(1), juce::Colours::blue.getARGB());
    EXPECT_EQ(rig.undo.getEditSerial(), steps + 1) << "one step for the whole drag";
    ASSERT_TRUE(rig.undo.undo());
    EXPECT_FALSE(rig.storedColour(1).has_value()) << "undo puts the palette colour back";
    EXPECT_FALSE(rig.undo.undo() && rig.storedColour(1).has_value());
}

TEST(MorphDotColour, APickThatEndsWhereItBeganRecordsNothingAndKeepsTheOriginal) {
    DotRig rig;
    const int steps = rig.undo.getEditSerial();
    ASSERT_TRUE(rig.map.showDotColour(0));
    const auto initial = rig.out.picker()->getCurrentColourForTest();
    rig.out.picker()->setCurrentColourForTest(juce::Colours::red);
    rig.out.picker()->setCurrentColourForTest(initial);
    rig.out.content.reset();
    EXPECT_EQ(rig.undo.getEditSerial(), steps);
    EXPECT_FALSE(rig.storedColour(0).has_value());
}

TEST(MorphDotColour, ThePickedColourIsWhatTheChipTheDotAndTheSavedStateWear) {
    DotRig rig;
    ASSERT_TRUE(rig.map.showDotColour(1));
    rig.out.picker()->setCurrentColourForTest(juce::Colours::orange);
    rig.out.content.reset();

    const auto& colors = synth::theme::themeOf(rig.map).colors;
    EXPECT_EQ(morphShapeColour(colors, 1, rig.map.getDots()[1].colour), juce::Colours::orange);
    EXPECT_EQ(morphShapeColour(colors, 0, rig.map.getDots()[0].colour), morphShapeColour(colors, 0))
        << "others keep theirs";

    const auto saved = rig.filter->getExtraState();
    FilterModule reloaded;
    reloaded.setExtraState(saved);
    ASSERT_GE(reloaded.getCustomShapes().shapes.size(), 2u);
    EXPECT_EQ(reloaded.getCustomShapes().shapes[1].colour, juce::Colours::orange.getARGB()) << "survives save and load";
    EXPECT_FALSE(reloaded.getCustomShapes().shapes[0].colour.has_value());
}

// ---------------------------------------------------------------------------------------------------------------
// The stored colour in the saved lists
// ---------------------------------------------------------------------------------------------------------------

TEST(MorphShapeColourFormat, FilterAndLfoListsRoundTripAColourAndOldListsLoadWithNone) {
    synth::CustomFilterShapes filter;
    filter.shapes.resize(2);
    filter.shapes[1].colour = 0xFF112233u;
    const auto filterBack = synth::CustomFilterShapes::fromVar(filter.toVar());
    ASSERT_EQ(filterBack.shapes.size(), 2u);
    EXPECT_FALSE(filterBack.shapes[0].colour.has_value());
    EXPECT_EQ(filterBack.shapes[1].colour, 0xFF112233u);

    // A lone unnamed centre shape with a colour must not collapse to the bare curve (which has no colour).
    auto lfo = synth::LfoCustomShapes::defaultShapes();
    ASSERT_TRUE(lfo.isPlainSingle());
    lfo.shapes[0].colour = 0xFFAA5500u;
    EXPECT_FALSE(lfo.isPlainSingle());
    EXPECT_EQ(synth::LfoCustomShapes::fromVar(lfo.toVar()).shapes[0].colour, 0xFFAA5500u);

    auto plain = synth::LfoCustomShapes::defaultShapes();
    EXPECT_FALSE(synth::LfoCustomShapes::fromVar(plain.toVar()).shapes[0].colour.has_value());

    // Model-authored lists never set a colour.
    juce::var untrusted = lfo.toVar();
    synth::LfoCustomShapes parsed;
    if (synth::LfoCustomShapes::fromUntrustedVar(untrusted, parsed))
        EXPECT_FALSE(parsed.shapes[0].colour.has_value());
}
