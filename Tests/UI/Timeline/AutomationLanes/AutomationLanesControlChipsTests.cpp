// AutomationLanesControlChipsTests.cpp -- the control chips on a lane header and a mod lane row, the "+" row of the
// controls that are not shown, and Edit controls mode with its tray: every chip runs the action the lane menu has, "+"
// lists exactly the hidden controls, edit mode shows/hides (click, Space, drag header <-> tray) and reorders (drag,
// Alt+Left/Right), the set is shared by the lanes of a kind and kept in the user settings, and the motion lands at
// once under Reduce Motion. Driven through the components' own mouse and key entry points, as the other lane tests.

#include "../../../TestSettingsHelpers.h"
#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AutomationLanesMenuFixture.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/AutomationLanes/AddModulator/AddModulatorPicker.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControls.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"

using namespace automation_lanes_test;
using namespace lane_menu_test;
using synth::ui::AutomationLaneHeaderComponent;
using synth::ui::LaneControl;
using synth::ui::LaneControlKind;
using synth::ui::LaneControlStrip;
using synth::ui::ModulatorInfo;
using synth::ui::ModulatorRow;

namespace {
using Controls = std::vector<LaneControl>;

// A host that offers parameters, a CV jack on every parameter, one LFO routing on every lane, and records what the
// mod rows ask of it.
struct ChipHost : PickHost {
    std::vector<ModulatorInfo> modulators;
    bool modulatable = true;
    int removed = 0;
    juce::String shown;

    std::vector<ModulatorInfo> getModulators(const juce::String& uuid, const juce::String&) override {
        return uuid.startsWith("atten-") ? std::vector<ModulatorInfo>{} : modulators;
    }
    bool canModulate(const juce::String&, const juce::String&) override { return modulatable; }
    bool canChangeModulatorSource(const ModulatorInfo&) override { return true; }
    std::vector<LfoChoice> getChangeSourceChoices(const juce::String&, const juce::String&) override {
        LfoChoice current;
        current.uuid = "lfo-1";
        current.name = "LFO 1";
        return {current};
    }
    void removeModulator(const ModulatorInfo&) override { ++removed; }
    void showNodeOnCanvas(const juce::String& uuid) override { shown = uuid; }
    juce::String getNodeDisplayName(const juce::String&) override { return "Filter"; }
};

ModulatorInfo routing() {
    ModulatorInfo info;
    info.sourceUuid = "lfo-1";
    info.sourceTitle = "LFO 1";
    info.isLfo = true;
    info.attenuverterUuid = "atten-1";
    info.targetUuid = "node-cutoff";
    info.paramId = "cutoff";
    info.targetChannel = 2;
    return info;
}

// Track "Bass" with two open lanes (cutoff over res), and optionally one LFO routing on each.
struct ChipPanel : LanesPanel {
    ChipHost host;
    synth::TrackId track;
    synth::LaneId lane;
    synth::LaneId lane2;

    explicit ChipPanel(bool withModulator = false) {
        track = doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane = addLane(track, "cutoff");
        lane2 = addLane(track, "res");
        host.offer("Filter 1", "Cutoff", "cutoff", {0.0f, 100.0f, 50.0f});
        host.offer("Filter 1", "Resonance", "res", {0.0f, 100.0f, 50.0f});
        host.offer("Oscillator 1", "Detune", "detune", {-100.0f, 100.0f, 0.0f});
        if (withModulator)
            host.modulators = {routing()};
        panel.setTrackHeaderHost(&host);
        panel.setTrackAutomationExpanded(track, true);
        panel.getViewState().firstVisibleBeat = 0.0;
    }
    ~ChipPanel() { panel.setTrackHeaderHost(nullptr); }

    AutomationLaneHeaderComponent& header(synth::LaneId id) { return *panel.laneHeaderForTest(id); }
    LaneControlStrip& strip(synth::LaneId id) { return header(id).getControlStrip(); }
    ModulatorRow& modRow(synth::LaneId id) { return *panel.modulatorRowForTest(id, 0); }
};

juce::Point<float> centreOf(juce::Component& c) { return c.getLocalBounds().getCentre().toFloat(); }

void press(juce::Component& c) {
    c.mouseDown(makeClickEvent(c, centreOf(c), leftButton()));
    c.mouseUp(makeClickEvent(c, centreOf(c), leftButton()));
}

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

synth::ui::LaneRowButton* rowButton(LaneControlStrip& strip, std::optional<LaneControl> control) {
    if (strip.getRow() == nullptr)
        return nullptr;
    for (auto& b : strip.getRow()->getButtons())
        if (b->getControl() == control)
            return b.get();
    return nullptr;
}

// Opens the "+" row and presses the button of `control` in it, the way a click does.
void pressInRow(LaneControlStrip& strip, std::optional<LaneControl> control) {
    strip.openRow();
    auto* button = rowButton(strip, control);
    ASSERT_NE(button, nullptr);
    clickButton(*button);
}

Controls shownOn(LaneControlStrip& strip) { return strip.visibleControls(); }

// The set kind's saved text, empty when it was never saved.
juce::String savedText(juce::ApplicationProperties& props, LaneControlKind kind) {
    return props.getUserSettings()->getValue(synth::ui::laneControlSettingsKey(kind));
}

struct SettingsKeysGuard {
    synth::test::PersistedKeysGuard guard{
        juce::StringArray{synth::ui::laneControlSettingsKey(LaneControlKind::Automation),
                          synth::ui::laneControlSettingsKey(LaneControlKind::Mod)}};
};
} // namespace

// ---- The catalogue and the saved set ----

TEST(LaneControlsCatalogueTest, EachKindOffersItsOwnActionsInTheMenuOrderWithTheDocumentedDefaults) {
    using synth::ui::defaultLaneControls;
    using synth::ui::laneControlCatalogue;
    EXPECT_EQ(defaultLaneControls(LaneControlKind::Automation),
              (Controls{LaneControl::ReadWrite, LaneControl::AddMod}));
    EXPECT_EQ(defaultLaneControls(LaneControlKind::Mod), (Controls{LaneControl::ChangeSource}))
        << "no mod routing can be muted without removing it, so a mod lane starts with Change source";
    const auto mod = laneControlCatalogue(LaneControlKind::Mod);
    EXPECT_EQ(std::count(mod.begin(), mod.end(), LaneControl::Mute), 0);
    const auto automation = laneControlCatalogue(LaneControlKind::Automation);
    EXPECT_EQ(automation.front(), LaneControl::ReadWrite);
    EXPECT_EQ(automation.back(), LaneControl::Remove);
}

TEST(LaneControlsCatalogueTest, ASavedSetDropsUnknownAndForeignIdsAndRepeats) {
    using synth::ui::parseLaneControls;
    using synth::ui::serialiseLaneControls;
    EXPECT_EQ(parseLaneControls(LaneControlKind::Automation, "addMod,bogus,readWrite,addMod,changeSource"),
              (Controls{LaneControl::AddMod, LaneControl::ReadWrite}))
        << "changeSource is a mod lane control";
    EXPECT_EQ(serialiseLaneControls({LaneControl::Mute, LaneControl::Remove}), "mute,remove");
    EXPECT_TRUE(parseLaneControls(LaneControlKind::Mod, "").empty());
}

TEST(LaneControlsCatalogueTest, EveryTooltipIsNameColonDescription) {
    EXPECT_EQ(synth::ui::laneControlTooltip(LaneControl::Mute, LaneControlKind::Automation), "Mute: Mute this lane");
    EXPECT_EQ(synth::ui::laneControlTooltip(LaneControl::ChangeParameter, LaneControlKind::Automation),
              "Change parameter: Point this lane at another control");
    EXPECT_EQ(synth::ui::laneControlName(LaneControl::Remove, LaneControlKind::Mod), "Remove modulator");
    EXPECT_EQ(synth::ui::laneControlName(LaneControl::Remove, LaneControlKind::Automation), "Remove lane");
}

// ---- The chips on the header ----

TEST(AutomationLanesControlChipsTest, AnAutomationHeaderShowsReadWriteAndAddModThenPlusWithNamesAndTooltips) {
    ChipPanel f;
    auto& strip = f.strip(f.lane);
    EXPECT_EQ(shownOn(strip), (Controls{LaneControl::ReadWrite, LaneControl::AddMod}));
    auto* rw = strip.chipFor(LaneControl::ReadWrite);
    auto* add = strip.chipFor(LaneControl::AddMod);
    ASSERT_NE(rw, nullptr);
    EXPECT_TRUE(rw->isVisible());
    EXPECT_TRUE(add->isVisible());
    EXPECT_FALSE(strip.chipFor(LaneControl::Mute)->isVisible()) << "hidden controls are not on the header";
    EXPECT_EQ(rw->getTitle(), "Read / Write");
    EXPECT_TRUE(rw->getTooltip().startsWith("Read / Write: Read or write automation")) << rw->getTooltip();
    EXPECT_EQ(add->getTitle(), "Add mod");
    EXPECT_EQ(add->getTooltip(), "Add mod: Add a modulation source to this control");
    EXPECT_TRUE(rw->getWantsKeyboardFocus());
    EXPECT_LT(rw->getX(), add->getX()) << "in set order";
    EXPECT_LT(add->getX(), strip.moreButton().getX()) << "then +";
    EXPECT_EQ(strip.moreButton().getTitle(), "More actions");
    EXPECT_EQ(strip.moreButton().getTooltip(), "More actions for this lane");
    EXPECT_LE(rw->getHeight(), 22);
}

TEST(AutomationLanesControlChipsTest, TheReadWriteChipOpensTheFiveModesAndAPickIsOneUndoStep) {
    ChipPanel f;
    MenuCapture menu;
    auto& header = f.header(f.lane);
    press(*header.getControlStrip().chipFor(LaneControl::ReadWrite));
    ASSERT_EQ(menu.count, 1);
    EXPECT_EQ(menu.itemTexts(), (juce::StringArray{"Off", "Read", "Touch", "Latch", "Write"}));
    const auto* current = findMenuItem(menu.menu, "Read");
    ASSERT_NE(current, nullptr);
    EXPECT_TRUE(current->isTicked);

    header.applyMenuChoice(AutomationLaneHeaderComponent::kRecordModeMenuIdBase + (int)synth::LaneRecordMode::Latch);
    EXPECT_EQ(f.doc.getLane(f.lane)->recordMode, (int)synth::LaneRecordMode::Latch);
    EXPECT_TRUE(header.getControlStrip().chipFor(LaneControl::ReadWrite)->getTooltip().endsWith("(Latch)"));
    f.undo.undo();
    EXPECT_EQ(f.doc.getLane(f.lane)->recordMode, (int)synth::LaneRecordMode::Read);
}

TEST(AutomationLanesControlChipsTest, TheAddModChipOpensTheAddModulatorPickerAndIsDisabledWithoutACvInput) {
    ChipPanel f;
    std::unique_ptr<synth::ui::ModMatrixPicker> picker;
    synth::ui::test_hooks::addModulatorPickerHookForTest() = [&picker](auto p) { picker = std::move(p); };
    auto& strip = f.strip(f.lane);
    clickButton(*strip.chipFor(LaneControl::AddMod));
    EXPECT_NE(picker, nullptr);

    f.host.modulatable = false;
    f.header(f.lane).refreshFromDoc();
    EXPECT_FALSE(strip.chipFor(LaneControl::AddMod)->isEnabled());
    EXPECT_TRUE(strip.chipFor(LaneControl::AddMod)->getTooltip().endsWith("(no CV input)"));
    synth::ui::test_hooks::addModulatorPickerHookForTest() = nullptr;
}

TEST(AutomationLanesControlChipsTest, TheMuteChipSetsTheRecordModeToOffAndPutsThePreviousModeBack) {
    ChipPanel f;
    auto& header = f.header(f.lane);
    auto& strip = header.getControlStrip();
    strip.toggleControl(LaneControl::Mute); // show it
    auto* mute = strip.chipFor(LaneControl::Mute);
    ASSERT_TRUE(mute->isVisible());
    header.applyRecordModeChoice((int)synth::LaneRecordMode::Touch + 1);
    EXPECT_FALSE(mute->isLit());

    clickButton(*mute);
    EXPECT_EQ(f.doc.getLane(f.lane)->recordMode, (int)synth::LaneRecordMode::Off);
    EXPECT_TRUE(mute->isLit()) << "a muted lane's chip reads as pressed";
    clickButton(*mute);
    EXPECT_EQ(f.doc.getLane(f.lane)->recordMode, (int)synth::LaneRecordMode::Touch) << "the mode it replaced";
    f.undo.undo();
    EXPECT_EQ(f.doc.getLane(f.lane)->recordMode, (int)synth::LaneRecordMode::Off) << "each is one undo step";
}

// ---- The + row ----

TEST(AutomationLanesControlChipsTest, PlusOpensARowWithExactlyTheHiddenControlsInOrderEndingWithEditControls) {
    ChipPanel f;
    auto& strip = f.strip(f.lane);
    clickButton(strip.moreButton());
    ASSERT_TRUE(strip.isRowOpen());
    juce::StringArray labels;
    for (auto& b : strip.getRow()->getButtons())
        labels.add(b->getTitle());
    EXPECT_EQ(labels, (juce::StringArray{"Change parameter", "Mute", "Duplicate", "Move to track", "Create custom LFO",
                                         "Remove lane", "Edit controls"}));
    EXPECT_EQ(strip.getRow()->getParentComponent(), &f.panel) << "it floats over the timeline";
    EXPECT_GT(strip.getRow()->getWidth(), 0) << "no motion here: it lands at once";
    EXPECT_LE(strip.getRow()->getRight(), f.panel.getWidth()) << "clamped inside the timeline";
    EXPECT_GE(strip.getRow()->getX(), f.header(f.lane).getRight() - 2) << "beside the header";
    for (auto& b : strip.getRow()->getButtons()) {
        EXPECT_EQ(b->getWantsKeyboardFocus(), b->isEnabled()) << "a Tab stop unless disabled: " << b->getTitle();
        EXPECT_FALSE(b->getTooltip().isEmpty());
    }
}

TEST(AutomationLanesControlChipsTest, TheRowClosesOnEscOnAClickAwayAndOnAPress) {
    ChipPanel f;
    auto& strip = f.strip(f.lane);
    strip.openRow();
    ASSERT_TRUE(strip.isRowOpen());
    EXPECT_TRUE(strip.getRow()->keyPressed(key(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(strip.isRowOpen());

    strip.openRow();
    strip.getRow()->mouseDown(makeClickEvent(f.panel, {500.0f, 300.0f}, leftButton()));
    EXPECT_FALSE(strip.isRowOpen()) << "a press anywhere else closes it";

    strip.openRow();
    strip.getRow()->mouseDown(makeClickEvent(*strip.getRow(), {3.0f, 3.0f}, leftButton()));
    EXPECT_TRUE(strip.isRowOpen()) << "a press inside it does not";

    clickButton(strip.moreButton());
    EXPECT_FALSE(strip.isRowOpen()) << "+ toggles it";
}

TEST(AutomationLanesControlChipsTest, EveryRowButtonRunsTheSameActionAsTheLaneMenuItem) {
    ChipPanel f;
    auto& strip = f.strip(f.lane);
    {
        PickerCapture picker;
        pressInRow(strip, LaneControl::ChangeParameter);
        EXPECT_NE(picker.picker, nullptr) << "Change parameter opens the picker";
        EXPECT_FALSE(strip.isRowOpen()) << "a press closes the row";
    }
    {
        PickerCapture picker;
        pressInRow(strip, LaneControl::Duplicate);
        EXPECT_NE(picker.picker, nullptr) << "Duplicate opens the picker for the copy";
    }
    {
        f.doc.addTrack(synth::TrackKind::Midi, "Lead");
        MenuCapture menu;
        f.header(f.lane).refreshFromDoc();
        pressInRow(strip, LaneControl::MoveToTrack);
        ASSERT_EQ(menu.count, 1);
        EXPECT_TRUE(menu.itemTexts().contains("Lead")) << "the lane menu's own Move to track list";
    }
    {
        const auto before = f.doc.getTrack(f.track)->lanes.size();
        pressInRow(strip, LaneControl::Remove);
        EXPECT_EQ(f.doc.getTrack(f.track)->lanes.size(), before - 1) << "Remove lane deletes the lane";
        f.undo.undo();
        EXPECT_EQ(f.doc.getTrack(f.track)->lanes.size(), before);
    }
}

TEST(AutomationLanesControlChipsTest, CreateCustomLfoStaysListedButDisabledWithItsReasonUntilARangeIsSelected) {
    ChipPanel f;
    auto& strip = f.strip(f.lane);
    strip.openRow();
    auto* button = rowButton(strip, LaneControl::CreateCustomLfo);
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(button->isEnabled());
    EXPECT_TRUE(button->getTooltip().endsWith("(select a range first)")) << button->getTooltip();
}

TEST(AutomationLanesControlChipsTest, ChipsThatDoNotFitMoveBehindPlusAndAppearInTheRow) {
    LanesPanel base; // for a doc and undo manager only
    const auto track = base.doc.addTrack(synth::TrackKind::Midi, "Bass");
    const auto lane = base.addLane(track, "cutoff");
    juce::Component root;
    root.setSize(600, 300);
    synth::ui::LaneControlSet set;
    set.setOverlayParent(&root);
    AutomationLaneHeaderComponent header(base.doc, lane, nullptr, &base.undo, &set);
    root.addAndMakeVisible(header);
    header.setBounds(0, 0, 110, 40);
    auto& strip = header.getControlStrip();

    EXPECT_EQ(strip.visibleControls(), (Controls{LaneControl::ReadWrite})) << "only the first chip fits";
    EXPECT_FALSE(strip.chipFor(LaneControl::AddMod)->isVisible());
    strip.openRow();
    ASSERT_NE(strip.getRow(), nullptr);
    EXPECT_EQ(strip.getRow()->getButtons().front()->getControl(), LaneControl::AddMod)
        << "the overflowing chip is first";

    header.setBounds(0, 0, 220, 40);
    EXPECT_EQ(strip.visibleControls(), (Controls{LaneControl::ReadWrite, LaneControl::AddMod})) << "and back when room";
}

// ---- Edit controls ----

TEST(AutomationLanesControlChipsTest, EditControlsOutlinesTheHeaderOpensTheTrayAndMakesRoomForTheLanesBelow) {
    ChipPanel f;
    auto& strip = f.strip(f.lane);
    const int before = f.header(f.lane2).getY();
    pressInRow(strip, std::nullopt);

    ASSERT_TRUE(strip.isEditing());
    EXPECT_EQ(strip.moreButton().getTitle(), "Editing controls");
    EXPECT_EQ(strip.moreButton().getTooltip(), "Editing controls: press Esc or Done to finish");
    EXPECT_GT(strip.editAmount(), 0.99f);
    ASSERT_NE(strip.getTray(), nullptr);
    EXPECT_EQ(strip.getTray()->getHeight(), synth::ui::LaneControlStrip::kTrayHeight);
    EXPECT_EQ(strip.getTray()->getY(), f.header(f.lane).getBottom() - 1) << "attached under the header";
    EXPECT_EQ(f.header(f.lane2).getY(), before + 36) << "the lane below makes room for the tray and its gap";
    EXPECT_EQ(strip.getTray()->getDoneButton().getTitle(), "Done editing controls");
    EXPECT_EQ(strip.getTray()->getDoneButton().getTooltip(), "Leave edit mode (Esc)");
    EXPECT_EQ(strip.chipFor(LaneControl::Mute)->getParentComponent(), strip.getTray()) << "hidden controls sit in it";
    EXPECT_EQ(strip.chipFor(LaneControl::ReadWrite)->getParentComponent(), &strip);
    EXPECT_EQ(strip.chipFor(LaneControl::Mute)->getTitle(), "Show Mute on the lane") << "a press shows it, not mutes";
    EXPECT_EQ(strip.chipFor(LaneControl::Mute)->getTooltip(), "Mute (hidden): click to show on the lane");
    EXPECT_EQ(strip.chipFor(LaneControl::ReadWrite)->getTitle(), "Hide Read / Write from the lane");
    EXPECT_TRUE(strip.moreButton().getToggleState()) << "the pencil is pressed";

    clickButton(strip.getTray()->getDoneButton());
    EXPECT_EQ(strip.chipFor(LaneControl::ReadWrite)->getTitle(), "Read / Write") << "named for its action again";
    EXPECT_FALSE(strip.isEditing());
    EXPECT_EQ(f.header(f.lane2).getY(), before) << "the rows move back up";
    EXPECT_EQ(strip.getTray()->getHeight(), 0);
}

TEST(AutomationLanesControlChipsTest, EscOnTheHeaderAndInTheTrayLeavesEditMode) {
    ChipPanel f;
    auto& strip = f.strip(f.lane);
    strip.enterEdit();
    ASSERT_TRUE(strip.isEditing());
    EXPECT_TRUE(f.header(f.lane).keyPressed(key(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(strip.isEditing());

    strip.enterEdit();
    EXPECT_TRUE(strip.getTray()->keyPressed(key(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(strip.isEditing());

    strip.enterEdit();
    EXPECT_TRUE(strip.chipFor(LaneControl::AddMod)->keyPressed(key(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(strip.isEditing()) << "from a chip too";
}

TEST(AutomationLanesControlChipsTest, AClickOrSpaceOnAChipShowsOrHidesItAndTheSetIsSharedByEveryLaneOfTheKind) {
    ChipPanel f;
    auto& strip = f.strip(f.lane);
    strip.enterEdit();

    press(*strip.chipFor(LaneControl::Mute)); // hidden -> shown, at the end
    EXPECT_EQ(shownOn(strip), (Controls{LaneControl::ReadWrite, LaneControl::AddMod, LaneControl::Mute}));
    EXPECT_EQ(strip.chipFor(LaneControl::Mute)->getParentComponent(), &strip);
    EXPECT_EQ(shownOn(f.strip(f.lane2)), shownOn(strip)) << "another automation lane follows at once";
    EXPECT_FALSE(f.strip(f.lane2).isEditing()) << "...without entering edit mode";

    EXPECT_TRUE(strip.chipFor(LaneControl::AddMod)->keyPressed(key(juce::KeyPress::spaceKey))); // shown -> hidden
    EXPECT_EQ(shownOn(strip), (Controls{LaneControl::ReadWrite, LaneControl::Mute}));
    EXPECT_EQ(strip.chipFor(LaneControl::AddMod)->getParentComponent(), strip.getTray());
    EXPECT_EQ(shownOn(f.strip(f.lane2)), shownOn(strip));
}

TEST(AutomationLanesControlChipsTest, AltLeftAndRightMoveTheFocusedChipAndLeftRightAreKeptForFocus) {
    ChipPanel f;
    auto& strip = f.strip(f.lane);
    strip.enterEdit();
    auto* add = strip.chipFor(LaneControl::AddMod);
    EXPECT_TRUE(add->keyPressed(key(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier)));
    EXPECT_EQ(shownOn(strip), (Controls{LaneControl::AddMod, LaneControl::ReadWrite}));
    EXPECT_LT(add->getX(), strip.chipFor(LaneControl::ReadWrite)->getX());
    EXPECT_TRUE(add->keyPressed(key(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier)));
    EXPECT_EQ(shownOn(strip), (Controls{LaneControl::AddMod, LaneControl::ReadWrite})) << "already first: no change";
    EXPECT_TRUE(add->keyPressed(key(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier)));
    EXPECT_EQ(shownOn(strip), (Controls{LaneControl::ReadWrite, LaneControl::AddMod}));

    // A bare Left/Right is the walk between chips: used, and changes nothing.
    EXPECT_TRUE(add->keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_TRUE(add->keyPressed(key(juce::KeyPress::rightKey)));
    EXPECT_EQ(shownOn(strip), (Controls{LaneControl::ReadWrite, LaneControl::AddMod}));
    EXPECT_FALSE(add->keyPressed(key(juce::KeyPress::upKey))) << "Up/Down still walk the lanes";
}

TEST(AutomationLanesControlChipsTest, DraggingAChipFromTheHeaderIntoTheTrayHidesItAndBackShowsItAtTheDropPoint) {
    ChipPanel f;
    auto& strip = f.strip(f.lane);
    strip.enterEdit();
    auto* add = strip.chipFor(LaneControl::AddMod);
    auto* tray = strip.getTray();
    const auto intoTray = add->getLocalPoint(tray, tray->getLocalBounds().getCentre()).toFloat();

    dragAcross(*add, centreOf(*add), intoTray);
    EXPECT_EQ(shownOn(strip), (Controls{LaneControl::ReadWrite})) << "dropped on the tray: hidden";
    EXPECT_EQ(add->getParentComponent(), tray);
    EXPECT_EQ(add->getAlpha(), 1.0f) << "and drawn again";

    // Back: from the tray onto the header, left of the first chip.
    auto* rw = strip.chipFor(LaneControl::ReadWrite);
    const auto onHeader = add->getLocalPoint(&strip, juce::Point<int>(2, strip.getHeight() / 2)).toFloat();
    dragAcross(*add, centreOf(*add), onHeader);
    EXPECT_EQ(shownOn(strip), (Controls{LaneControl::AddMod, LaneControl::ReadWrite})) << "dropped at the left end";
    EXPECT_EQ(add->getParentComponent(), &strip);
    EXPECT_LT(add->getX(), rw->getX());
}

TEST(AutomationLanesControlChipsTest, DraggingAChipSidewaysInTheHeaderReordersAndADropOffTheHeaderChangesNothing) {
    ChipPanel f;
    auto& strip = f.strip(f.lane);
    strip.enterEdit();
    auto* add = strip.chipFor(LaneControl::AddMod);
    const auto toLeft = add->getLocalPoint(&strip, juce::Point<int>(1, strip.getHeight() / 2)).toFloat();
    dragAcross(*add, centreOf(*add), toLeft);
    EXPECT_EQ(shownOn(strip), (Controls{LaneControl::AddMod, LaneControl::ReadWrite}));

    const auto nowhere = add->getLocalPoint(&f.panel, juce::Point<int>(900, 500)).toFloat();
    dragAcross(*add, centreOf(*add), nowhere);
    EXPECT_EQ(shownOn(strip), (Controls{LaneControl::AddMod, LaneControl::ReadWrite})) << "dropped away: back in place";
}

TEST(AutomationLanesControlChipsTest, OnlyOneLaneIsInEditModeAtATime) {
    ChipPanel f;
    f.strip(f.lane).enterEdit();
    ASSERT_TRUE(f.strip(f.lane).isEditing());
    f.strip(f.lane2).enterEdit();
    EXPECT_TRUE(f.strip(f.lane2).isEditing());
    EXPECT_FALSE(f.strip(f.lane).isEditing()) << "the first one left edit mode";
}

TEST(AutomationLanesControlChipsTest, TheSetIsKeptInTheUserSettingsAndReadBackByAFreshComponent) {
    SettingsKeysGuard guard;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    props.getUserSettings()->removeValue(synth::ui::laneControlSettingsKey(LaneControlKind::Automation));
    {
        ChipPanel f;
        f.panel.setApplicationProperties(&props);
        auto& strip = f.strip(f.lane);
        strip.enterEdit();
        strip.toggleControl(LaneControl::AddMod);
        strip.toggleControl(LaneControl::Duplicate);
        EXPECT_EQ(savedText(props, LaneControlKind::Automation), "readWrite,duplicate");
    }
    ChipPanel fresh;
    fresh.panel.setApplicationProperties(&props);
    EXPECT_EQ(shownOn(fresh.strip(fresh.lane)), (Controls{LaneControl::ReadWrite, LaneControl::Duplicate}));
    EXPECT_EQ(shownOn(fresh.strip(fresh.lane2)), (Controls{LaneControl::ReadWrite, LaneControl::Duplicate}));
}

TEST(AutomationLanesControlChipsTest, TheAutomationAndModSetsAreSeparate) {
    ChipPanel f(true);
    auto& mod = f.modRow(f.lane).getControlStrip();
    EXPECT_EQ(shownOn(mod), (Controls{LaneControl::ChangeSource}));
    f.strip(f.lane).enterEdit();
    f.strip(f.lane).toggleControl(LaneControl::ReadWrite);
    EXPECT_EQ(shownOn(mod), (Controls{LaneControl::ChangeSource})) << "mod lanes keep their own set";
}

// ---- Motion ----

TEST(AutomationLanesControlChipsTest, TheTraySlidesInStepsAndLandsAtOnceUnderReduceMotion) {
    FadeAnimateGuard animate(synth::ui::AnimationMode::full);
    ChipPanel f;
    auto& strip = f.strip(f.lane);
    strip.enterEdit();
    EXPECT_TRUE(strip.isAnimating());
    EXPECT_EQ(strip.trayExtent(), 0) << "it has not moved yet";
    strip.stepForTest(0.5f);
    EXPECT_GT(strip.trayExtent(), 0);
    EXPECT_LT(strip.trayExtent(), 36);
    strip.stepForTest(1.0f);
    EXPECT_EQ(strip.trayExtent(), 36);
    EXPECT_FALSE(strip.isAnimating());

    strip.exitEdit();
    EXPECT_TRUE(strip.isAnimating());
    strip.stepForTest(1.0f);
    EXPECT_EQ(strip.trayExtent(), 0);

    synth::ui::setAnimationMode(synth::ui::AnimationMode::reduced);
    strip.enterEdit();
    EXPECT_EQ(strip.trayExtent(), 36) << "Reduce Motion: at once";
    EXPECT_FALSE(strip.isAnimating());
    strip.exitEdit();
    EXPECT_EQ(strip.trayExtent(), 0);
}

TEST(AutomationLanesControlChipsTest, ThePlusRowSlidesOpenLeftToRightAndLandsAtOnceUnderReduceMotion) {
    FadeAnimateGuard animate(synth::ui::AnimationMode::full);
    ChipPanel f;
    auto& strip = f.strip(f.lane);
    strip.openRow();
    ASSERT_NE(strip.getRow(), nullptr);
    EXPECT_TRUE(strip.getRow()->isAnimating());
    EXPECT_EQ(strip.getRow()->getWidth(), 0);
    strip.getRow()->stepForTest(0.5f);
    EXPECT_GT(strip.getRow()->getWidth(), 0);
    EXPECT_LT(strip.getRow()->getWidth(), strip.getRow()->getFullWidth());
    strip.getRow()->stepForTest(1.0f);
    EXPECT_EQ(strip.getRow()->getWidth(), strip.getRow()->getFullWidth());
    strip.closeRow();
    strip.getRow()->stepForTest(1.0f);
    EXPECT_EQ(strip.getRow()->getParentComponent(), nullptr);

    synth::ui::setAnimationMode(synth::ui::AnimationMode::reduced);
    strip.openRow();
    EXPECT_EQ(strip.getRow()->getWidth(), strip.getRow()->getFullWidth());
    EXPECT_FALSE(strip.getRow()->isAnimating());
}

// ---- Mod lanes ----

TEST(AutomationLanesControlChipsTest, AModLaneShowsChangeSourceAndTheRowListsShowInCanvasAndRemove) {
    ChipPanel f(true);
    auto& strip = f.modRow(f.lane).getControlStrip();
    auto* change = strip.chipFor(LaneControl::ChangeSource);
    ASSERT_NE(change, nullptr);
    EXPECT_TRUE(change->isVisible());
    EXPECT_EQ(change->getTitle(), "Change source");
    EXPECT_EQ(change->getTooltip(), "Change source: Point this lane at another modulation source");
    EXPECT_EQ(strip.chipFor(LaneControl::Mute), nullptr) << "a routing has no non-destructive mute";

    synth::ui::test_hooks::changeSourcePickerHookForTest() = nullptr;
    std::unique_ptr<synth::ui::ModMatrixPicker> picker;
    synth::ui::test_hooks::changeSourcePickerHookForTest() = [&picker](auto p) { picker = std::move(p); };
    clickButton(*change);
    EXPECT_NE(picker, nullptr);
    synth::ui::test_hooks::changeSourcePickerHookForTest() = nullptr;

    strip.openRow();
    juce::StringArray labels;
    for (auto& b : strip.getRow()->getButtons())
        labels.add(b->getTitle());
    EXPECT_EQ(labels, (juce::StringArray{"Show in canvas", "Remove modulator", "Edit controls"}));
    clickButton(*rowButton(strip, LaneControl::ShowInCanvas));
    EXPECT_EQ(f.host.shown, "lfo-1");
    pressInRow(strip, LaneControl::Remove);
    EXPECT_EQ(f.host.removed, 1);
}

TEST(AutomationLanesControlChipsTest, AModLaneEditModeMakesRoomUnderTheRowAndShowsHiddenControlsInItsTray) {
    ChipPanel f(true);
    auto& row = f.modRow(f.lane);
    auto& strip = row.getControlStrip();
    const int before = f.header(f.lane2).getY();
    strip.enterEdit();
    EXPECT_EQ(f.header(f.lane2).getY(), before + 36);
    EXPECT_EQ(strip.getTray()->getY(), row.getBottom() - 1);
    EXPECT_EQ(strip.chipFor(LaneControl::Remove)->getParentComponent(), strip.getTray());
    press(*strip.chipFor(LaneControl::Remove));
    EXPECT_EQ(shownOn(strip), (Controls{LaneControl::ChangeSource, LaneControl::Remove}));
    strip.exitEdit();
    EXPECT_EQ(f.header(f.lane2).getY(), before);
}

TEST(AutomationLanesControlChipsTest, TheTrayWrapsToAnotherRowInsteadOfOverlappingChips) {
    ChipPanel f;
    auto& strip = f.strip(f.lane);
    strip.enterEdit();
    ASSERT_EQ(strip.trayRows(), 1) << "six hidden chips and Done fit one row";
    const int before = f.header(f.lane2).getY();

    strip.toggleControl(LaneControl::ReadWrite);
    strip.toggleControl(LaneControl::AddMod); // every control hidden
    auto* tray = strip.getTray();
    ASSERT_EQ(strip.trayRows(), 2);
    EXPECT_EQ(tray->getHeight(), 2 * synth::ui::LaneControlStrip::kTrayHeight);
    EXPECT_EQ(f.header(f.lane2).getY(), before + 34) << "the rows below move by the second row";
    juce::Array<juce::Rectangle<int>> boxes;
    for (const auto control : synth::ui::laneControlCatalogue(LaneControlKind::Automation)) {
        const auto bounds = strip.chipFor(control)->getBounds();
        EXPECT_EQ(strip.chipFor(control)->getParentComponent(), tray);
        EXPECT_TRUE(tray->getLocalBounds().contains(bounds)) << "fully inside the tray";
        EXPECT_FALSE(bounds.intersects(tray->getDoneButton().getBounds()));
        for (const auto& other : boxes)
            EXPECT_FALSE(bounds.intersects(other)) << "chips never overlap";
        boxes.add(bounds);
    }

    strip.toggleControl(LaneControl::ReadWrite);
    strip.toggleControl(LaneControl::AddMod); // six hidden again: back to one row
    EXPECT_EQ(strip.trayRows(), 1);
    EXPECT_EQ(f.header(f.lane2).getY(), before);
}
