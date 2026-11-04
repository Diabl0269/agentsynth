// MixerColourDotTests.cpp (docs/mixer/panel.md#the-colour-dot): clicking the coloured dot in a mixer header opens the
// track's colour picker -- the same one its Timeline swatch opens -- and a pick recolours the track, its channel macro
// and every mixer column as one undo step. The dot is a Tab stop with a name and a tooltip. A real off-screen
// MainComponent; the picker is captured through the panel's test hook instead of opening a CallOutBox.
#include "../../TestWait.h"
#include "Mixer/ChannelMacroLookup.h"
#include "MixerHeaderTestRig.h"
#include "MixerNativeHost.h"
#include "MixerZonesTestRig.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>

namespace {

using mixer_header_test::clickThroughMouse;
using mixer_header_test::HeaderRig;
using synth::ui::ColourPickerPopup;
using synth::ui::MixerPanelComponent;
using Route = MixerPanelComponent::ColourRoute;

struct Captured {
    int opens = 0;
    Route route = Route::None;
    std::unique_ptr<ColourPickerPopup> popup;
    juce::Rectangle<int> area;
};

void capturePickers(HeaderRig& rig, Captured& captured) {
    rig.panel().setShowColourPickerHookForTest(
        [&captured](Route route, std::unique_ptr<ColourPickerPopup> popup, juce::Rectangle<int> area) {
            ++captured.opens;
            captured.route = route;
            captured.popup = std::move(popup);
            captured.area = area;
        });
}

juce::Colour firstHeaderColour(HeaderRig& rig) { return rig.firstColumn().getHeaderForTest().getColour(); }

juce::Colour macroColourOfFirstColumn(HeaderRig& rig) {
    const auto* macro = synth::nearestChannelMacro(rig.mc.getAudioEngine().getGraph(),
                                                   rig.mc.getGraphEditor().getMacros(), rig.firstColumn().getUuid());
    return macro != nullptr ? macro->colour : juce::Colours::transparentBlack;
}

} // namespace

TEST(MixerColourDotTest, TheDotIsANamedKeyboardReachableButtonWithATooltip) {
    HeaderRig rig;
    auto& header = rig.firstColumn().getHeaderForTest();
    auto& dot = header.getColourDotForTest();
    ASSERT_TRUE(dot.isVisible());
    EXPECT_TRUE(dot.getWantsKeyboardFocus());
    EXPECT_TRUE(dot.isEnabled());
    EXPECT_EQ(dot.getTitle(), header.getDisplayName() + " colour");
    EXPECT_EQ(dot.getTooltip(), "Change this channel's colour  (C)") << "the tooltip names the colour key";
    EXPECT_EQ(dot.getColour(), header.getColour());
    EXPECT_FALSE(dot.getBounds().isEmpty());
    EXPECT_FALSE(dot.getBounds().intersects(header.getNameLabelForTest().getBounds()));
}

// Regression test for FRO1206: the dot claimed Space, so after a click on a strip Space opened a picker, not play.
TEST(MixerColourDotTest, TheDotTakesReturnAndLeavesSpaceToPlay) {
    HeaderRig rig;
    Captured captured;
    capturePickers(rig, captured);
    auto& dot = static_cast<juce::Component&>(rig.firstColumn().getHeaderForTest().getColourDotForTest());
    EXPECT_FALSE(dot.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_TRUE(dot.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    synth::test::flushMessageQueue(); // the click is posted as a command message
    EXPECT_EQ(captured.opens, 1);
}

TEST(MixerColourDotTest, ClickingTheDotOpensTheTracksPickerAnchoredOnTheDot) {
    HeaderRig rig;
    Captured captured;
    capturePickers(rig, captured);
    ASSERT_EQ(rig.panel().getColourRouteForTest(rig.firstColumn().getUuid()), Route::Track);

    clickThroughMouse(rig.firstColumn().getHeaderForTest().getColourDotForTest());
    EXPECT_EQ(captured.opens, 1);
    EXPECT_EQ(captured.route, Route::Track);
    ASSERT_NE(captured.popup, nullptr);
    EXPECT_EQ(captured.popup->getCurrentColourForTest(), juce::Colour(rig.doc().getTracks()[0].colourArgb));
    EXPECT_EQ(captured.area, rig.firstColumn().getHeaderForTest().getColourDotForTest().getScreenBounds());
}

TEST(MixerColourDotTest, ReturnOnTheFocusedDotOpensThePickerToo) {
    HeaderRig rig;
    Captured captured;
    capturePickers(rig, captured);
    auto& dot = rig.firstColumn().getHeaderForTest().getColourDotForTest();

    EXPECT_TRUE(static_cast<juce::Component&>(dot).keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    synth::test::waitUntil([&] { return (captured.opens) == (1); });
    EXPECT_EQ(captured.opens, 1);
    EXPECT_NE(captured.popup, nullptr);
}

TEST(MixerColourDotTest, APickRecoloursTheTrackItsMacroAndTheMixerHeaderThenUndoRestoresAllThree) {
    HeaderRig rig;
    Captured captured;
    capturePickers(rig, captured);
    const auto trackId = rig.doc().getTracks()[0].id;
    const auto originalTrack = rig.doc().getTracks()[0].colourArgb;
    const auto originalMacro = macroColourOfFirstColumn(rig);
    const juce::Colour picked(0xff12ab34);
    ASSERT_NE(picked.getARGB(), originalTrack);

    clickThroughMouse(rig.firstColumn().getHeaderForTest().getColourDotForTest());
    ASSERT_NE(captured.popup, nullptr);

    captured.popup->setCurrentColourForTest(picked); // a live preview: no undo step yet
    EXPECT_EQ(rig.doc().getTrack(trackId)->colourArgb, picked.getARGB());
    EXPECT_EQ(firstHeaderColour(rig), picked) << "the mixer header re-tints while the picker is still open";
    EXPECT_EQ(macroColourOfFirstColumn(rig), picked) << "and so does the track's channel macro";

    captured.popup->commitForTest();
    captured.popup.reset();
    EXPECT_EQ(rig.doc().getTrack(trackId)->colourArgb, picked.getARGB());
    EXPECT_EQ(macroColourOfFirstColumn(rig), picked);
    rig.panel().rebuild();
    EXPECT_EQ(firstHeaderColour(rig), picked) << "a rebuilt column reads the new colour too";

    ASSERT_TRUE(rig.mc.getUndoManager().undo()) << "the whole pick is one undo step";
    EXPECT_EQ(rig.doc().getTrack(trackId)->colourArgb, originalTrack);
    EXPECT_EQ(macroColourOfFirstColumn(rig), originalMacro);
    rig.panel().rebuild();
    EXPECT_EQ(firstHeaderColour(rig), juce::Colour(originalTrack));
}

TEST(MixerColourDotTest, ClosingThePickerWithNoNetChangeRestoresTheColourAndRecordsNothing) {
    HeaderRig rig;
    Captured captured;
    capturePickers(rig, captured);
    const auto trackId = rig.doc().getTracks()[0].id;
    const auto original = rig.doc().getTracks()[0].colourArgb;
    clickThroughMouse(rig.firstColumn().getHeaderForTest().getColourDotForTest());
    ASSERT_NE(captured.popup, nullptr);

    captured.popup->setCurrentColourForTest(juce::Colour(0xff991122));
    captured.popup->setCurrentColourForTest(juce::Colour(original));
    captured.popup->commitForTest();
    EXPECT_EQ(rig.doc().getTrack(trackId)->colourArgb, original);
    EXPECT_EQ(firstHeaderColour(rig), juce::Colour(original));
}

TEST(MixerColourDotTest, ABusRecoloursItsMacroThroughTheMacroPicker) {
    HeaderRig rig;
    Captured captured;
    capturePickers(rig, captured);
    auto* bus = rig.busColumn();
    ASSERT_NE(bus, nullptr);
    ASSERT_EQ(rig.panel().getColourRouteForTest(bus->getUuid()), Route::Macro);
    EXPECT_TRUE(bus->getHeaderForTest().getColourDotForTest().isEnabled());

    clickThroughMouse(bus->getHeaderForTest().getColourDotForTest());
    EXPECT_EQ(captured.route, Route::Macro);
    ASSERT_NE(captured.popup, nullptr);

    const juce::Colour picked(0xff778899);
    captured.popup->setCurrentColourForTest(picked);
    captured.popup->commitForTest();
    captured.popup.reset();
    rig.panel().rebuild();
    EXPECT_EQ(rig.busColumn()->getHeaderForTest().getColour(), picked);
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    rig.panel().rebuild();
    EXPECT_NE(rig.busColumn()->getHeaderForTest().getColour(), picked);
}

TEST(MixerColourDotTest, DirectAndMasterShowNoDotAndADisabledDotTakesNoClickAndNoFocus) {
    HeaderRig rig;
    ASSERT_NE(rig.panel().getMasterColumnForTest(), nullptr);
    EXPECT_FALSE(rig.panel().getMasterColumnForTest()->getHeaderForTest().getColourDotForTest().isVisible());
    if (auto* direct = rig.panel().getDirectColumnForTest())
        EXPECT_FALSE(direct->getHeaderForTest().getColourDotForTest().isVisible());

    synth::ui::MixerColumnHeader header;
    header.setSize(140, 24);
    header.setColour(juce::Colours::red);
    header.setColourEditable(false);
    EXPECT_FALSE(header.getColourDotForTest().isEnabled());
    EXPECT_FALSE(header.getColourDotForTest().getWantsKeyboardFocus());
    EXPECT_NE(header.getColourDotForTest().getTooltip(), "Change this channel's colour");
    int opened = 0;
    header.onColourClicked = [&opened](juce::Rectangle<int>) { ++opened; };
    clickThroughMouse(header.getColourDotForTest());
    EXPECT_EQ(opened, 0) << "a disabled dot takes no click";
}

TEST(MixerColourDotTest, PressingTheDotAndDraggingReordersTheColumnInsteadOfOpeningThePicker) {
    HeaderRig rig;
    Captured captured;
    capturePickers(rig, captured);
    rig.mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);
    auto* column = rig.panel().getStripColumnForTest(0);
    auto& dot = column->getHeaderForTest().getColourDotForTest();
    const auto start = dot.getLocalBounds().toFloat().getCentre();

    dot.mouseDown(makeClickEvent(dot, start));
    dot.mouseDrag(makeDragEvent(dot, start.translated(60.0f, 0.0f), start));
    EXPECT_TRUE(rig.panel().isColumnReorderActiveForTest()) << "the dot is part of the header's drag handle";
    ASSERT_TRUE(rig.panel().sendEscapeToColumnDragForTest());
    dot.mouseUp(makeClickEvent(dot, start.translated(60.0f, 0.0f)));
    HeaderRig::pumpMessages();
    EXPECT_EQ(captured.opens, 0) << "the release of a drag is not a click";
}

// The colour key (C by default, rebindable) opens the focused strip's colour picker on its
// dot, Return on the dot opens it once, and Space on the dot is left to the window's play command. Keys travel the real
// ComponentPeer dispatch on a native window holding the real panel of an off-screen MainComponent.

namespace {

using namespace synth::test::mixer;
using ColourRoute = synth::ui::MixerPanelComponent::ColourRoute;

juce::KeyPress letter(char c) { return juce::KeyPress(c, juce::ModifierKeys::noModifiers, 0); }

// Counts the pickers the panel opens and where they are anchored, instead of showing a CallOutBox.
struct PickerCounter {
    int opened = 0;
    juce::Rectangle<int> anchor;
    void hook(synth::ui::MixerPanelComponent& panel) {
        panel.setShowColourPickerHookForTest(
            [this](ColourRoute, std::unique_ptr<synth::ui::ColourPickerPopup>, auto bounds) {
                ++opened;
                anchor = bounds;
            });
    }
};

// The window's own commands: counts the keys that reach it, as the app's Space (play/stop) would see them.
struct WindowKeys {
    int spaces = 0;
    int others = 0;
    void hook(NativeHost& host) {
        host.onUnhandledKey = [this](const juce::KeyPress& key) {
            (key == juce::KeyPress(juce::KeyPress::spaceKey) ? spaces : others)++;
            return true;
        };
    }
};

bool panelHasKeyboard(NativeHost& host) {
    host.panel.grabKeyboardFocus();
    return host.panel.hasKeyboardFocus(false);
}

} // namespace

// Regression test for FRO1206: no key opened a strip's colour picker; the dot was only reachable by mouse or Tab.
TEST(MixerColourKeyTest, COpensTheFocusedStripsPickerOnItsDot) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    MixerZonesRig r(2);
    NativeHost host(*r.panel);
    if (!panelHasKeyboard(host))
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";
    PickerCounter picker;
    picker.hook(*r.panel);
    WindowKeys window;
    window.hook(host);

    ASSERT_TRUE(pressThroughPeer(host, juce::KeyPress(juce::KeyPress::rightKey)));
    ASSERT_TRUE(pressThroughPeer(host, juce::KeyPress(juce::KeyPress::rightKey)));
    ASSERT_EQ(r.panel->getFocusedColumnIndexForTest(), 1);
    EXPECT_TRUE(pressThroughPeer(host, letter('c')));
    EXPECT_EQ(picker.opened, 1);
    EXPECT_EQ(window.others, 0) << "the panel consumed the key";
    const auto& dot = r.panel->getStripColumnForTest(1)->getHeaderForTest().getColourDotForTest();
    EXPECT_EQ(picker.anchor, dot.getScreenBounds()) << "the picker opens on the focused strip's own dot";
}

TEST(MixerColourKeyTest, CIsLeftToBubbleOnAColumnWithNothingToRecolour) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    MixerZonesRig r(1);
    NativeHost host(*r.panel);
    if (!panelHasKeyboard(host))
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";
    PickerCounter picker;
    picker.hook(*r.panel);
    WindowKeys window;
    window.hook(host);
    for (int i = 0; i < r.panel->getColumnCount(); ++i)
        pressThroughPeer(host, juce::KeyPress(juce::KeyPress::rightKey));
    ASSERT_EQ(r.panel->getFocusedColumnIndexForTest(), r.panel->getColumnCount() - 1) << "Master, the last column";
    pressThroughPeer(host, letter('c'));
    EXPECT_EQ(picker.opened, 0);
    EXPECT_EQ(window.others, 1) << "Master has no colour, so the key goes on to the window";
}

TEST(MixerColourKeyTest, TheKeyIsRebindableAndTheDotsTooltipNamesIt) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    MixerZonesRig r(1);
    auto& shortcuts = r.mc.getShortcutManager();
    EXPECT_EQ(ShortcutManager::getCategory("mixerOpenColour"), ShortcutCategory::Mixer);
    EXPECT_EQ(shortcuts.getBinding("mixerOpenColour"), letter('c'));
    auto& dot = r.panel->getStripColumnForTest(0)->getHeaderForTest().getColourDotForTest();
    EXPECT_EQ(dot.getTooltip(), "Change this channel's colour  (C)");

    shortcuts.setBinding("mixerOpenColour", letter('k'));
    // The Settings tab saves after a rebind, which notifies onBindingsChanged; saving here would write the user's file.
    ASSERT_TRUE(shortcuts.onBindingsChanged);
    shortcuts.onBindingsChanged();
    EXPECT_EQ(dot.getTooltip(), "Change this channel's colour  (K)") << "the tooltip follows a rebind";

    NativeHost host(*r.panel);
    if (!panelHasKeyboard(host))
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";
    PickerCounter picker;
    picker.hook(*r.panel);
    WindowKeys window;
    window.hook(host);
    pressThroughPeer(host, juce::KeyPress(juce::KeyPress::rightKey));
    ASSERT_EQ(r.panel->getFocusedColumnIndexForTest(), 0);
    pressThroughPeer(host, letter('c'));
    EXPECT_EQ(picker.opened, 0) << "the old key no longer opens the picker";
    pressThroughPeer(host, letter('k'));
    EXPECT_EQ(picker.opened, 1);
}

// Regression test for FRO1206: Space on a focused colour dot opened the picker instead of playing, and a click on a
// strip often leaves the keyboard on the dot.
TEST(MixerColourKeyTest, OnTheDotReturnOpensThePickerOnceAndSpaceGoesOnToPlay) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    MixerZonesRig r(1);
    NativeHost host(*r.panel);
    PickerCounter picker;
    picker.hook(*r.panel);
    WindowKeys window;
    window.hook(host);
    auto& dot = r.panel->getStripColumnForTest(0)->getHeaderForTest().getColourDotForTest();
    dot.grabKeyboardFocus();
    if (!dot.hasKeyboardFocus(false))
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";

    pressThroughPeer(host, juce::KeyPress(juce::KeyPress::spaceKey));
    synth::test::flushMessageQueue(); // a click is posted as a command message
    EXPECT_EQ(picker.opened, 0) << "Space on the dot opened the picker";
    EXPECT_EQ(window.spaces, 1) << "Space reaches the window's play command";

    EXPECT_TRUE(pressThroughPeer(host, juce::KeyPress(juce::KeyPress::returnKey)));
    synth::test::flushMessageQueue();
    EXPECT_EQ(picker.opened, 1) << "Return on the dot opens the picker exactly once";
}
