// ShapeWindowPitchGridTests.cpp -- the Filter shape window's pitch grids: the Notes and Harmonics lines against note
// frequencies, steps down to a semitone, the separate Level row, the snap switches, the key's out-of-key notes, the
// played-note root, the remembered (and migrated) choice, the Ring helpers' peaks in the filter's own response, and the
// Draw pen's peaks on the chosen notes or harmonics (one per line, every shape at its top on the line).
#include "../../../TestSettingsHelpers.h"
#include "../../../TestWait.h"
#include "../../Accessibility/TabOrderHelpers.h"
#include "../../Layout/SnapButtonExpect.h"
#include "Modules/Filter/CustomFilterBlend.h"
#include "Modules/Filter/CustomFilterShapes.h"
#include "Modules/LFOModule.h"
#include "Modules/Lfo/LfoCustomWave.h"
#include "Music/SongKey.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/Layout/PopupHost.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/PitchGrid.h"
#include "UI/ModuleViews/ShapeEditor/PitchGridRows.h"
#include "UI/ModuleViews/ShapeEditor/ShapeDrawing.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeRingHelpers.h"
#include "UI/ModuleViews/ShapeEditor/ShapeStackPanel.h"
#include "UI/ModuleViews/ShapeEditor/ShapeWindowPrefs.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace synth::ui;
using namespace synth::ui::test;
using synth::CustomFilterShapes;
using synth::LfoCustomWave;
using GridLine = CurveEditorComponent::GridLine;

namespace {

const synth::MusicalScale kCMajor{0, 0xAB5, "Major"};
const synth::MusicalScale kAMinor{9, 0x5AD, "Minor"};

struct PitchKeysGuard {
    synth::test::PersistedKeysGuard guard{
        juce::StringArray{shapewindowprefs::gridKey("filter"), shapewindowprefs::gridKindKey("filter"),
                          shapewindowprefs::gridStepsKey("filter"), shapewindowprefs::gridLevelKey("filter"),
                          shapewindowprefs::snapKey("filter"), shapewindowprefs::gridOwnKeyKey("filter"),
                          shapewindowprefs::gridRootKey("filter")}};
};

const GridLine* lineAt(const CurveEditorComponent::CurveGrid& grid, double hz) {
    const double x = CustomFilterShapes::hzToX((float)hz);
    for (const auto& line : grid.xLines)
        if (std::abs(line.x - x) < 1e-5)
            return &line;
    return nullptr;
}

pitchgrid::Choice choiceOf(pitchgrid::Kind kind, float levelDb = 6.0f, int steps = 1) {
    pitchgrid::Choice choice;
    choice.kind = kind;
    choice.levelDb = levelDb;
    choice.stepsPerOctave = steps;
    return choice;
}

CurveGeometryConfig editorGeometryConfig() {
    CurveGeometryConfig config;
    config.explicitVisibleRange = 1.0;
    config.zeroSegmentPx = 0.0f;
    return config;
}

void dragNode(CurveEditorComponent& curve, int index, double x, float level) {
    const CurveEditorGeometry geometry(curve.getModel(), curve.getLocalBounds().toFloat(), editorGeometryConfig());
    const auto from = geometry.nodePosition(index);
    const juce::Point<float> to(geometry.xForTime(x), geometry.yForLevel(level));
    curve.mouseDown(curveLeftClick(curve, from));
    curve.mouseDrag(curveLeftDragPass(curve, to, from)); // Option: the node may cross its neighbours
    curve.mouseUp(curveLeftDrag(curve, to, from));
}

/** The node nearest `x` (the curve's x), -1 when none is within `tolerance`. */
int nodeNear(const CurveEditorComponent& curve, double x, double tolerance = 1e-4) {
    for (int i = 0; i < curve.getModel().getNumNodes(); ++i)
        if (std::abs(curve.getModel().getNode(i).x - x) < tolerance)
            return i;
    return -1;
}

void selectNode(CurveEditorComponent& curve, int index) {
    curve.selectNodeRelative(0);
    while (curve.getSelectedIndex() != index && curve.selectNodeRelative(1) >= 0) {
    }
}

/** The filter's asked-for response (dB on the engine's grid) at the selected shape's own map position. */
std::vector<float> responseOf(ShapeRig& rig, int shape = 0) {
    const auto shapes = rig.adapter->getShapes();
    const auto& pos = shapes[(size_t)shape].position;
    return synth::customfilter::blendedGridDb(rig.filter->getCustomShapes(), pos[0], pos[1]);
}

float responseAtHz(const std::vector<float>& db, double hz) {
    const double x = CustomFilterShapes::hzToX((float)hz);
    return db[(size_t)std::lround(x * (double)(db.size() - 1))];
}

} // namespace

TEST(ShapeWindowPitchGrid, NotesLinesSitOnEveryEqualTemperedNoteAndTheCsAreNamedLandmarks) {
    const auto grid = pitchgrid::gridFor(choiceOf(pitchgrid::Kind::notes), {});
    ASSERT_TRUE(grid.has_value());
    const auto* a4 = lineAt(*grid, 440.0);
    ASSERT_NE(a4, nullptr);
    EXPECT_EQ(a4->tier, GridLine::Tier::snap);
    EXPECT_EQ(a4->kind, GridLine::Kind::note);
    EXPECT_EQ(a4->name, juce::String::fromUTF8("A4 \xc2\xb7 440 Hz"));
    const auto* c4 = lineAt(*grid, pitchgrid::noteHz(60));
    ASSERT_NE(c4, nullptr);
    EXPECT_EQ(c4->tier, GridLine::Tier::landmark);
    EXPECT_EQ(c4->label, "C4");
    for (const auto& line : grid->xLines) {
        const double midi = pitchgrid::midiOf(CustomFilterShapes::xToHz((float)line.x));
        EXPECT_NEAR(midi, std::round(midi), 1e-3) << "every line is a note";
    }
    EXPECT_EQ(grid->yDivisions, 10) << "the 6 dB Level row over the 60 dB range";
}

TEST(ShapeWindowPitchGrid, NotesOutsideTheKeyAreDottedTierAndNeverSnappedTo) {
    PitchKeysGuard keys;
    pitchgrid::Context context;
    context.key = kAMinor;
    const auto grid = pitchgrid::gridFor(choiceOf(pitchgrid::Kind::notes), context);
    ASSERT_TRUE(grid.has_value());
    EXPECT_EQ(lineAt(*grid, pitchgrid::noteHz(61))->tier, GridLine::Tier::outOfKey) << "C#4 is not in A minor";
    EXPECT_EQ(lineAt(*grid, pitchgrid::noteHz(60))->tier, GridLine::Tier::landmark) << "C4 is";

    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(choiceOf(pitchgrid::Kind::notes));
    ASSERT_TRUE(rows.getKeyField().onPickOwn);
    rows.getKeyField().onPickOwn(kAMinor);
    auto& curve = editor.getCurveEditor();
    // A hair above C#4: the nearest in-key note is C4 or D4, never the dotted C#4.
    dragNode(curve, 3, CustomFilterShapes::hzToX((float)pitchgrid::noteHz(61) * 1.002f), 0.8f);
    EXPECT_EQ(nodeNear(curve, CustomFilterShapes::hzToX((float)pitchgrid::noteHz(61))), -1);
    EXPECT_GE(nodeNear(curve, CustomFilterShapes::hzToX((float)pitchgrid::noteHz(62))), 0) << "lands on D4";
}

TEST(ShapeWindowPitchGrid, HarmonicsAreTheRootsMultiplesWithThePowersOfTwoAsLandmarks) {
    pitchgrid::Context context;
    context.rootHz = 110.0;
    const auto grid = pitchgrid::gridFor(choiceOf(pitchgrid::Kind::harmonics), context);
    ASSERT_TRUE(grid.has_value());
    EXPECT_EQ((int)grid->xLines.size(), 181) << "110 Hz to 19.9 kHz";
    EXPECT_EQ(lineAt(*grid, 220.0)->tier, GridLine::Tier::landmark);
    EXPECT_EQ(lineAt(*grid, 220.0)->label, "x2");
    EXPECT_EQ(lineAt(*grid, 330.0)->tier, GridLine::Tier::snap);
    EXPECT_EQ(lineAt(*grid, 330.0)->kind, GridLine::Kind::harmonic);
    EXPECT_TRUE(lineAt(*grid, 330.0)->name.contains("330 Hz"));
    EXPECT_EQ(lineAt(*grid, 110.0 * 17)->label, "") << "only the first 16 are labelled";
}

TEST(ShapeWindowPitchGrid, OctaveStepsGoDownToASemitone) {
    const auto grid = pitchgrid::gridFor(choiceOf(pitchgrid::Kind::octaves, 6.0f, 12), {});
    ASSERT_TRUE(grid.has_value());
    EXPECT_NE(lineAt(*grid, 1000.0 * std::pow(2.0, 1.0 / 12.0)), nullptr);
    EXPECT_NE(lineAt(*grid, 1000.0), nullptr);
    EXPECT_EQ(pitchgrid::stepNames(), juce::StringArray({"1 oct", "1/2", "1/3", "1/4", "1/6", "1/12"}));
}

TEST(ShapeWindowPitchGrid, TheLevelRowIsItsOwnChoiceAndBothOffIsFree) {
    auto notes = pitchgrid::gridFor(choiceOf(pitchgrid::Kind::notes, 0.0f), {});
    ASSERT_TRUE(notes.has_value());
    EXPECT_EQ(notes->yDivisions, 0) << "Level Off: no level lines";
    EXPECT_EQ(pitchgrid::gridFor(choiceOf(pitchgrid::Kind::off, 3.0f), {})->yDivisions, 20);
    EXPECT_TRUE(pitchgrid::gridFor(choiceOf(pitchgrid::Kind::off, 3.0f), {})->xLines.empty());
    EXPECT_FALSE(pitchgrid::gridFor(choiceOf(pitchgrid::Kind::off, 0.0f), {}).has_value());
    EXPECT_EQ(pitchgrid::levelNames(), juce::StringArray({"Off", "12", "6", "3", "1.5"}));
}

TEST(ShapeWindowPitchGrid, SnapOffLeavesBothTheFrequencyAndTheLevelFree) {
    PitchKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto choice = choiceOf(pitchgrid::Kind::notes);
    choice.snap = false;
    editor.getPitchRows()->setChoice(choice);
    EXPECT_FALSE(editor.getPitchRows()->getSnap().getToggleState());
    auto& curve = editor.getCurveEditor();
    const double x = CustomFilterShapes::hzToX(452.0f);
    dragNode(curve, 3, x, CustomFilterShapes::dbToY(-4.0f));
    const int node = nodeNear(curve, x, 2e-3);
    ASSERT_GE(node, 0) << "452 Hz stays put, between A4 and A#4";
    EXPECT_NEAR(curve.getModel().getNode(node).y, CustomFilterShapes::dbToY(-4.0f), 1e-4) << "the level is free too";
}

TEST(ShapeWindowPitchGrid, AnAltArrowMoveStepsToTheNextNoteOfTheKey) {
    PitchKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(choiceOf(pitchgrid::Kind::notes));
    rows.getKeyField().onPickOwn(kCMajor);
    auto& curve = editor.getCurveEditor();
    const auto xOf = [](int midi) { return CustomFilterShapes::hzToX((float)pitchgrid::noteHz(midi)); };
    dragNode(curve, 3, xOf(64), 0.8f); // E4
    const int index = nodeNear(curve, xOf(64));
    ASSERT_GE(index, 0);
    selectNode(curve, index);
    const juce::KeyPress altRight(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0);
    EXPECT_TRUE(curve.keyPressed(altRight));
    EXPECT_GE(nodeNear(curve, xOf(65)), 0) << "E4 to F4";
    EXPECT_TRUE(curve.keyPressed(altRight));
    EXPECT_GE(nodeNear(curve, xOf(67)), 0) << "F4 to G4, past the dotted F#4";
}

TEST(ShapeWindowPitchGrid, ThePlayedNoteRootFollowsTheNoteHeardOnThePitchInput) {
    PitchKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(choiceOf(pitchgrid::Kind::harmonics));
    rows.setRootId(PitchGridRows::kRootPlaying);
    EXPECT_NEAR(rows.rootHz(), 110.0, 1e-6) << "no note yet: A2";

    constexpr int kBlock = 64;
    rig.filter->prepareToPlay(44100.0, kBlock);
    juce::AudioBuffer<float> buffer(rig.filter->getTotalNumInputChannels(), kBlock);
    buffer.clear();
    for (int i = 0; i < kBlock; ++i)
        buffer.setSample(FilterModule::kPitchBase, i, 196.0f);
    juce::MidiBuffer midi;
    rig.filter->processBlock(buffer, midi);
    int changes = 0;
    rows.onGridChanged = [&changes] { ++changes; };
    rows.poll();
    EXPECT_GE(changes, 1);
    EXPECT_NEAR(rows.rootHz(), 196.0, 1e-3);
    const auto grid = rows.grid();
    ASSERT_TRUE(grid.has_value());
    EXPECT_NE(lineAt(*grid, 196.0 * 3), nullptr) << "the lines are G3's harmonics";
}

namespace {
/** Sends `hz` into the Filter's Pitch input and polls the rows. */
void playNote(ShapeRig& rig, PitchGridRows& rows, float hz) {
    constexpr int kBlock = 64;
    rig.filter->prepareToPlay(44100.0, kBlock);
    juce::AudioBuffer<float> buffer(rig.filter->getTotalNumInputChannels(), kBlock);
    buffer.clear();
    for (int i = 0; i < kBlock; ++i)
        buffer.setSample(FilterModule::kPitchBase, i, hz);
    juce::MidiBuffer midi;
    rig.filter->processBlock(buffer, midi);
    rows.poll();
}

double firstHarmonicHz(const PitchGridRows& rows) {
    const auto grid = rows.grid();
    for (const auto& line : grid->xLines)
        if (line.tier == GridLine::Tier::landmark)
            return (double)CustomFilterShapes::xToHz((float)line.x);
    return 0.0;
}
} // namespace

TEST(ShapeWindowPitchGrid, ThePlayedNoteLineGlidesToANewNoteInsteadOfJumping) {
    PitchKeysGuard keys;
    setReducedMotionForTest(false);
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(choiceOf(pitchgrid::Kind::harmonics));
    rows.setRootId(PitchGridRows::kRootPlaying);
    playNote(rig, rows, 110.0f);
    rows.getGlide().completeForTest();
    ASSERT_NEAR(firstHarmonicHz(rows), 110.0, 0.5);

    playNote(rig, rows, 220.0f);
    EXPECT_NEAR(rows.rootHz(), 220.0, 1e-3) << "the target is the new note at once";
    EXPECT_TRUE(rows.getGlide().isRunning());
    rows.getGlide().stepForTest(PitchGridRows::kGlideMs / 2);
    const double mid = firstHarmonicHz(rows);
    EXPECT_GT(mid, 111.0);
    EXPECT_LT(mid, 219.0) << "mid-glide the line is between the two notes";
    rows.getGlide().stepForTest(PitchGridRows::kGlideMs + 1);
    EXPECT_NEAR(firstHarmonicHz(rows), 220.0, 0.5);
    EXPECT_FALSE(rows.getGlide().isRunning());
    setReducedMotionForTest(std::nullopt);
}

TEST(ShapeWindowPitchGrid, ThePlayedNoteLineJumpsUnderReducedMotion) {
    PitchKeysGuard keys;
    setReducedMotionForTest(true);
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(choiceOf(pitchgrid::Kind::harmonics));
    rows.setRootId(PitchGridRows::kRootPlaying);
    playNote(rig, rows, 110.0f);
    playNote(rig, rows, 220.0f);
    EXPECT_FALSE(rows.getGlide().isRunning());
    EXPECT_NEAR(firstHarmonicHz(rows), 220.0, 0.5);
    setReducedMotionForTest(std::nullopt);
}

TEST(ShapeWindowPitchGrid, TheChoiceIsRememberedAndTheOldOneRowChoiceMigrates) {
    PitchKeysGuard keys;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    shapewindowprefs::saveText(&props, shapewindowprefs::gridKey("filter"), "1/3 oct");
    ShapeRig rig;
    rig.adapter->setApplicationProperties(&props);
    {
        ShapeListEditor editor(rig.adapter, 0);
        const auto choice = editor.getPitchRows()->getChoice();
        EXPECT_EQ(choice.kind, pitchgrid::Kind::octaves);
        EXPECT_EQ(choice.stepsPerOctave, 3);
        EXPECT_FLOAT_EQ(choice.levelDb, 6.0f);
        EXPECT_EQ(editor.getGridName(), "1/3 oct");
        editor.getPitchRows()->setChoice(choiceOf(pitchgrid::Kind::harmonics, 3.0f));
        editor.getPitchRows()->setRootId(PitchGridRows::rootIdOf(48));
    }
    EXPECT_TRUE(shapewindowprefs::loadText(&props, shapewindowprefs::gridKey("filter")).isEmpty())
        << "the old key is cleared once migrated";
    ShapeListEditor again(rig.adapter, 0);
    EXPECT_EQ(again.getPitchRows()->getChoice(), choiceOf(pitchgrid::Kind::harmonics, 3.0f));
    EXPECT_NEAR(again.getPitchRows()->rootHz(), pitchgrid::noteHz(48), 1e-6) << "C3 is remembered";
}

TEST(ShapeWindowPitchGrid, OnlyTheKindsContentShowsOnTheSecondRow) {
    PitchKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.setSize(ShapeListEditor::kWidth, ShapeListEditor::kPerNoteHeight);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(choiceOf(pitchgrid::Kind::octaves));
    EXPECT_TRUE(rows.getStepsSwitch().isVisible());
    EXPECT_FALSE(rows.getKeyField().isVisible());
    rows.setChoice(choiceOf(pitchgrid::Kind::notes));
    EXPECT_TRUE(rows.getKeyField().isVisible());
    EXPECT_FALSE(rows.getRootBox().isVisible());
    rows.setChoice(choiceOf(pitchgrid::Kind::harmonics));
    EXPECT_TRUE(rows.getRootBox().isVisible());
    EXPECT_FALSE(rows.getStepsSwitch().isVisible());
    EXPECT_TRUE(rows.getBounds().getBottom() <= editor.getCurveEditor().getY()) << "the rows sit above the curve";
    EXPECT_FALSE(editor.getRingButton().getBounds().intersects(editor.getDeleteButton().getBounds()));
    EXPECT_GT(editor.getRingButton().getWidth(), 0);
}

TEST(ShapeWindowPitchGrid, StackHarmonicsRingsAtTheRootsHarmonicsAsOneUndoStep) {
    PitchKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.getPitchRows()->setRootId(PitchGridRows::rootIdOf(45)); // A2, 110 Hz
    CapturedMenu menu;
    editor.showMenu = [&menu](juce::PopupMenu& m) { menu.capture(m); };
    editor.getRingButton().onClick();
    ASSERT_EQ(menu.items.size(), 4u);
    EXPECT_EQ(menu.items[0], "Tune selected peak to a note");

    const auto before = responseOf(rig);
    const int serial = rig.undo.getEditSerial();
    std::unique_ptr<juce::Component> popover;
    test_hooks::popoverLauncherForTest() = [&popover](std::unique_ptr<juce::Component> c, juce::Rectangle<int>) {
        popover = std::move(c);
        return nullptr;
    };
    ASSERT_TRUE(menu.run("Stack harmonics"));
    test_hooks::popoverLauncherForTest() = nullptr;
    ASSERT_NE(popover, nullptr) << "Stack harmonics asks for its options first";
    EXPECT_EQ(rig.undo.getEditSerial(), serial) << "nothing is changed until Stack is pressed";
    auto& panel = static_cast<ShapeStackPanel&>(*popover);
    panel.getStackButton().onClick();
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1) << "one undo step";
    const auto db = responseOf(rig);
    for (int n = 1; n <= ringhelpers::kHarmonics; ++n) {
        const float peak = responseAtHz(db, 110.0 * n);
        EXPECT_NEAR(peak, ringhelpers::kTopDb - ringhelpers::kHarmonicStepDb * (float)(n - 1), 2.5f)
            << "harmonic " << n;
        EXPECT_GT(peak, responseAtHz(db, 110.0 * (n + 0.5)) + 6.0f) << "a peak, not a plateau, at " << n;
    }
    EXPECT_NEAR(responseAtHz(db, 5000.0), ringhelpers::kFloorDb, 0.5f);
    rig.undo.undo();
    EXPECT_EQ(responseOf(rig), before);
}

TEST(ShapeWindowPitchGrid, TheStackPanelsOptionsSetTheRootCountStepAndWidthOfThePeaks) {
    PitchKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.getPitchRows()->setChoice(choiceOf(pitchgrid::Kind::harmonics));
    editor.getPitchRows()->setRootId(PitchGridRows::rootIdOf(45)); // A2
    std::unique_ptr<juce::Component> popover;
    test_hooks::popoverLauncherForTest() = [&popover](std::unique_ptr<juce::Component> c, juce::Rectangle<int>) {
        popover = std::move(c);
        return nullptr;
    };
    editor.openStackPanel();
    test_hooks::popoverLauncherForTest() = nullptr;
    ASSERT_NE(popover, nullptr);
    auto& panel = static_cast<ShapeStackPanel&>(*popover);

    const auto defaults = panel.getOptions();
    EXPECT_NEAR(defaults.rootHz, 110.0, 0.5);
    EXPECT_EQ(defaults.count, ringhelpers::kHarmonics);
    EXPECT_NEAR(defaults.stepDb, ringhelpers::kHarmonicStepDb, 1e-6f);

    // Every control is named, has a tooltip and takes Tab focus.
    for (juce::Component* c : std::initializer_list<juce::Component*>{
             &panel.getRootBox(), &panel.getCountBox(), &panel.getStepBox(), &panel.getWidthSwitch(),
             &panel.getCancelButton(), &panel.getStackButton()}) {
        EXPECT_TRUE(c->getTitle().isNotEmpty() || c == &panel.getWidthSwitch()) << c->getTitle();
        EXPECT_TRUE(c->getWantsKeyboardFocus());
        if (auto* tip = dynamic_cast<juce::SettableTooltipClient*>(c))
            EXPECT_TRUE(tip->getTooltip().isNotEmpty()) << c->getTitle();
    }

    panel.getRootBox().setSelectedId(1000 + 57, juce::sendNotificationSync); // A3
    panel.getCountBox().setSelectedId(4, juce::sendNotificationSync);
    panel.getStepBox().setSelectedId(1, juce::sendNotificationSync); // 0 dB
    panel.getWidthSwitch().setSelectedIndex(2, juce::sendNotificationSync);
    const auto chosen = panel.getOptions();
    EXPECT_NEAR(chosen.rootHz, 220.0, 0.5);
    EXPECT_EQ(chosen.count, 4);
    EXPECT_EQ(chosen.stepDb, 0.0f);
    EXPECT_GT(chosen.halfWidthOctaves, defaults.halfWidthOctaves);

    const int serial = rig.undo.getEditSerial();
    panel.getStackButton().onClick();
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
    const auto db = responseOf(rig);
    for (int n = 1; n <= 4; ++n)
        EXPECT_NEAR(responseAtHz(db, 220.0 * n), ringhelpers::kTopDb, 2.5f) << "equal peaks at harmonic " << n;
    EXPECT_NEAR(responseAtHz(db, 220.0 * 5.5), ringhelpers::kFloorDb, 0.5f) << "only four peaks";
    EXPECT_EQ(editor.getStatusColour(), synth::theme::themeOf(editor).colors.textMuted)
        << "a done message is not an error";
    EXPECT_NE(editor.getStatusColour(), synth::theme::themeOf(editor).colors.error);
}

TEST(ShapeWindowPitchGrid, ChordPeaksAndOctaveBellsLandOnTheirNotes) {
    PitchKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    rows.getKeyField().onPickOwn(kAMinor);
    rows.setChoice(choiceOf(pitchgrid::Kind::notes));
    editor.applyRingHelper(ringhelpers::Helper::chordPeaks);
    auto db = responseOf(rig);
    for (const int midi : {57, 60, 64, 69, 72, 76}) // A3 C4 E4, A4 C5 E5
        EXPECT_GT(responseAtHz(db, pitchgrid::noteHz(midi)), 8.0f) << midi;
    EXPECT_LT(responseAtHz(db, pitchgrid::noteHz(62)), 0.0f) << "D4 is not in the chord";

    rows.setRootId(PitchGridRows::rootIdOf(36)); // C2
    editor.applyRingHelper(ringhelpers::Helper::octaveBells);
    db = responseOf(rig);
    for (int octave = 0; octave < 7; ++octave)
        EXPECT_GT(responseAtHz(db, pitchgrid::noteHz(36) * std::pow(2.0, octave)), 6.0f) << octave;
}

TEST(ShapeWindowPitchGrid, TunePeakMovesTheSelectedPointToTheNearestNoteOfTheKey) {
    PitchKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.getPitchRows()->getKeyField().onPickOwn(kCMajor);
    auto& curve = editor.getCurveEditor();
    const double x = CustomFilterShapes::hzToX(455.0f); // between A4 and A#4: A4 is in C major
    dragNode(curve, 3, x, 0.95f);
    int index = nodeNear(curve, x, 1e-3);
    ASSERT_GE(index, 0);
    selectNode(curve, index);
    editor.applyRingHelper(ringhelpers::Helper::tunePeak);
    EXPECT_GE(nodeNear(curve, CustomFilterShapes::hzToX(440.0f)), 0);

    synth::LfoCustomWave flat;
    flat.points = {{0.0f, 0.5f, 0.0f}, {1.0f, 0.5f, 0.0f}};
    EXPECT_FALSE(ringhelpers::tunePeak(flat, -1, std::nullopt).has_value()) << "nothing between the ends to tune";
}

TEST(ShapeWindowPitchGrid, TabGoesThroughTheRowsInVisualOrderAndRingHelpersSitsAfterDraw) {
    PitchKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.setSize(ShapeListEditor::kWidth, ShapeListEditor::kPerNoteHeight);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(choiceOf(pitchgrid::Kind::harmonics));
    const auto walk = synth::test::walkTabOrder(editor);
    const auto at = [&walk](juce::Component& c) {
        const auto it = std::find(walk.all.begin(), walk.all.end(), &c);
        return it == walk.all.end() ? -1 : (int)(it - walk.all.begin());
    };
    const juce::Component* order[] = {&rows.getKindSwitch(),       &rows.getLevelSwitch(),  &rows.getSnap(),
                                      &rows.getSnapMode(),         &rows.getRootBox(),      editor.getGuidesToggle(),
                                      &editor.getCurveEditor(),    &editor.getDrawButton(), &editor.getRingButton(),
                                      &editor.getSaveShapeButton()};
    int previous = -1;
    for (const auto* c : order) {
        const int index = at(*const_cast<juce::Component*>(c));
        EXPECT_GT(index, previous) << c->getTitle();
        previous = index;
    }
    EXPECT_EQ(at(rows.getStepsSwitch()), -1) << "a hidden kind's control is not a stop";
    EXPECT_EQ(editor.getRingButton().getTitle(), "Ring helpers");
}

TEST(ShapeWindowPitchGrid, TheShapeEditorHasOneSnapButtonThatFlipsBothDirectionsBackAndForth) {
    static_assert(std::is_same_v<decltype(std::declval<synth::ui::PitchGridRows&>().getSnap()), SnapToggleButton&>);
    PitchKeysGuard keys;
    ShapeRig rig;
    synth::ui::ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    synth::ui::pitchgrid::Choice choice;
    choice.kind = synth::ui::pitchgrid::Kind::notes;
    choice.levelDb = 6.0f;
    rows.setChoice(choice);
    auto& button = rows.getSnap();
    expectSharedSnapButton(button, "shape editor snap");
    EXPECT_EQ(button.getTitle(), "Snap");
    ASSERT_TRUE(button.getToggleState());
    ASSERT_TRUE(rows.grid()->snapX && rows.grid()->snapY);
    button.triggerClick();
    synth::test::flushMessageQueue();
    EXPECT_FALSE(button.getToggleState());
    EXPECT_FALSE(rows.grid()->snapX) << "across is off";
    EXPECT_FALSE(rows.grid()->snapY) << "level is off";
    button.triggerClick();
    synth::test::flushMessageQueue();
    EXPECT_TRUE(button.getToggleState());
    EXPECT_TRUE(rows.grid()->snapX && rows.grid()->snapY) << "both come back on";
}

TEST(ShapeWindowPitchGrid, TheSnapChoiceIsRemembered) {
    PitchKeysGuard keys;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    ShapeRig rig;
    rig.adapter->setApplicationProperties(&props);
    {
        ShapeListEditor editor(rig.adapter, 0);
        auto choice = choiceOf(pitchgrid::Kind::octaves);
        choice.snap = false;
        editor.getPitchRows()->setChoice(choice);
    }
    ShapeListEditor again(rig.adapter, 0);
    EXPECT_FALSE(again.getPitchRows()->getChoice().snap);
    EXPECT_FALSE(again.getPitchRows()->getSnap().getToggleState());
}

// The Draw pen on a pitch grid.
namespace {

double xOfHz(double hz) { return (double)CustomFilterShapes::hzToX((float)hz); }
double xOfNote(int midi) { return xOfHz(pitchgrid::noteHz(midi)); }

pitchgrid::Choice gridOf(pitchgrid::Kind kind) {
    pitchgrid::Choice choice;
    choice.kind = kind;
    choice.levelDb = 0.0f; // levels free: the box spans exactly the pressed and dragged levels
    return choice;
}

/** A real-mouse stroke on the window's curve, press at (x0, level0) and drag to (x1, level1). */
struct Stroke {
    CurveEditorComponent& curve;
    juce::Point<float> anchor, last;

    juce::Point<float> at(double x, float level) const { return curve.pixelForDrawPoint({x, (double)level}); }
    void press(double x, float level) {
        anchor = last = at(x, level);
        curve.mouseDown(makeCurveMouseEvent(curve, anchor, juce::ModifierKeys::leftButtonModifier, false, anchor));
    }
    void drag(double x, float level) {
        last = at(x, level);
        curve.mouseDrag(makeCurveMouseEvent(curve, last, juce::ModifierKeys::leftButtonModifier, true, anchor));
    }
    void release() {
        curve.mouseUp(makeCurveMouseEvent(curve, last, juce::ModifierKeys::leftButtonModifier, true, anchor));
    }
};

/** The index of a point of `curve` equal to `p`, -1 when none is. */
int nodeAt(const LfoCustomWave& curve, const LfoCustomWave::Point& p) {
    for (size_t i = 0; i < curve.points.size(); ++i)
        if (curve.points[i].x == p.x && curve.points[i].y == p.y)
            return (int)i;
    return -1;
}

/** How many of `xs` the curve reaches `level` at (within a hair). */
int topsAt(const LfoCustomWave& curve, const std::vector<double>& xs, float level) {
    int n = 0;
    for (const double x : xs)
        n += std::abs(curve.evaluate((float)x) - level) < 0.01f ? 1 : 0;
    return n;
}

} // namespace

TEST(ShapeDrawPeaks, EveryPeriodicShapeTopsOutOnEachLineAndIsLowBetweenThem) {
    const std::vector<double> lines{0.2, 0.35, 0.6};
    for (const auto shape : {DrawShape::Sine, DrawShape::Triangle, DrawShape::Square, DrawShape::Saw}) {
        ShapeDrawStroke s;
        s.shape = shape;
        s.x0 = 0.1;
        s.x1 = 0.7;
        s.lo = 0.2f;
        s.hi = 0.8f;
        s.peaks = {0.05, 0.2, 0.35, 0.6, 0.9}; // two lines outside the stretch: ignored
        const auto drawn = drawOnCurve(LfoCustomWave::defaultWave(), s);
        ASSERT_TRUE(drawn.changed) << drawShapeName(shape);
        EXPECT_EQ(drawn.peaks, 3) << drawShapeName(shape);
        // Saw drops on its line: its top is the instant before it.
        std::vector<double> tops = lines;
        if (shape == DrawShape::Saw)
            for (auto& x : tops)
                x -= 1e-4;
        EXPECT_EQ(topsAt(drawn.curve, tops, 0.8f), 3) << drawShapeName(shape);
        if (shape != DrawShape::Saw) // Saw is at its middle between lines, every other shape at its bottom
            EXPECT_EQ(topsAt(drawn.curve, {(0.2 + 0.35) * 0.5, (0.35 + 0.6) * 0.5}, 0.2f), 2) << drawShapeName(shape);
        for (const auto& p : LfoCustomWave::defaultWave().points)
            if (p.x < 0.1f || p.x > 0.7f)
                EXPECT_GE(nodeAt(drawn.curve, p), 0) << "a point outside the stretch stays";
    }
}

TEST(ShapeDrawPeaks, OnNotesASineDragPutsOnePeakOnEachNoteOfTheKeyAndTheChipSaysSo) {
    PitchKeysGuard keys;
    ShapeRig rig;
    rig.adapter->setSongKeySource([] { return std::optional<synth::MusicalScale>(kCMajor); });
    ShapeListEditor editor(rig.adapter, 0);
    editor.getPitchRows()->setChoice(gridOf(pitchgrid::Kind::notes));
    editor.setPen(DrawShape::Sine);
    auto& curve = editor.getCurveEditor();

    Stroke stroke{curve, {}, {}};
    stroke.press(xOfNote(60), 0.3f); // C4
    stroke.drag(xOfNote(66), 0.8f);
    stroke.drag(xOfNote(72), 0.8f); // C5
    const auto chip = curve.getDrawGesture().chipText();
    EXPECT_EQ(chip, "8 peaks, one per note") << "C D E F G A B C: the black keys are out of the key";
    stroke.release();

    const auto stored = rig.adapter->getShapes()[0].curve;
    std::vector<double> inKey, outOfKey;
    for (int m = 60; m <= 72; ++m)
        (kCMajor.contains(m) ? inKey : outOfKey).push_back(xOfNote(m));
    EXPECT_EQ(topsAt(stored, inKey, 0.8f), 8);
    EXPECT_EQ(topsAt(stored, outOfKey, 0.8f), 0) << "no peak on C#, D#, F#, G#, A#";
}

TEST(ShapeDrawPeaks, OnHarmonicsTheRootSongKeyPutsAPeakOnEachHarmonicAsOneUndoStep) {
    PitchKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    EXPECT_EQ(rows.getRootBox().getSelectedId(), PitchGridRows::kRootSongKey)
        << "until a note is picked the root is the song key's";
    EXPECT_NEAR(rows.rootHz(), 110.0, 1e-6) << "no song key: A2";
    rows.setChoice(gridOf(pitchgrid::Kind::harmonics));
    editor.setPen(DrawShape::Triangle);
    auto& curve = editor.getCurveEditor();
    const auto before = rig.adapter->getShapes()[0].curve;

    Stroke stroke{curve, {}, {}};
    stroke.press(xOfHz(110.0), 0.4f);
    stroke.drag(xOfHz(500.0), 0.9f);
    stroke.drag(xOfHz(880.0), 0.9f);
    EXPECT_EQ(curve.getDrawGesture().chipText(), "8 peaks, one per harmonic") << "x1 to x8";
    const auto preview = curve.getDrawGesture().result().curve;
    stroke.release();

    const auto stored = rig.adapter->getShapes()[0].curve;
    EXPECT_TRUE(stored == preview) << "what the drag showed is what the drop wrote";
    std::vector<double> harmonics;
    for (int n = 1; n <= 8; ++n)
        harmonics.push_back(xOfHz(110.0 * n));
    EXPECT_EQ(topsAt(stored, harmonics, 0.9f), 8);
    rig.undo.undo();
    EXPECT_TRUE(rig.adapter->getShapes()[0].curve == before) << "one stroke, one undo step";
}

TEST(ShapeDrawPeaks, ASineAcrossEveryNoteThinsItsSidesToFitThePointCap) {
    PitchKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.getPitchRows()->setChoice(gridOf(pitchgrid::Kind::notes)); // no key: every semitone
    editor.setPen(DrawShape::Sine);
    auto& curve = editor.getCurveEditor();
    const auto lines = curve.drawPeaks().x;
    ASSERT_GT(lines.size() * 16, (size_t)LfoCustomWave::kMaxPoints) << "a full-resolution sine would not fit";

    Stroke stroke{curve, {}, {}};
    stroke.press(0.0, 0.3f);
    stroke.drag(0.5, 0.8f);
    stroke.drag(1.0, 0.8f);
    const auto drawn = curve.getDrawGesture().result();
    ASSERT_TRUE(drawn.changed) << drawn.refusal;
    EXPECT_EQ(drawn.peaks, (int)lines.size());
    EXPECT_LE((int)drawn.curve.points.size(), LfoCustomWave::kMaxPoints);
    EXPECT_EQ(topsAt(drawn.curve, lines, 0.8f), (int)lines.size());
    stroke.release();
}

TEST(ShapeDrawPeaks, EnterWithAPenOnAPitchGridPlacesTheSamePeaksAsTheMouse) {
    PitchKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(gridOf(pitchgrid::Kind::harmonics));
    rows.setRootId(PitchGridRows::rootIdOf(57)); // A3, 220 Hz
    editor.setPen(DrawShape::Triangle);          // two points a peak: every harmonic of A3 fits the point cap
    ASSERT_TRUE(editor.applyPenToSelection()) << "nothing selected: the whole curve";

    const auto stored = rig.adapter->getShapes()[0].curve;
    std::vector<double> harmonics;
    for (int n = 1; 220.0 * n <= CustomFilterShapes::kMaxHz; ++n)
        harmonics.push_back(xOfHz(220.0 * n));
    const auto range = rig.adapter->getDrawRange();
    EXPECT_EQ(topsAt(stored, harmonics, range.hi), (int)harmonics.size());
}

TEST(ShapeDrawPeaks, WithSnapOffAPitchGridLeavesTheCountToTheDrag) {
    PitchKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto choice = gridOf(pitchgrid::Kind::notes);
    choice.snap = false;
    editor.getPitchRows()->setChoice(choice);
    editor.setPen(DrawShape::Sine);
    auto& curve = editor.getCurveEditor();
    EXPECT_TRUE(curve.drawPeaks().x.empty());
    curve.getDrawGesture().setCycleWidth(0.1);

    Stroke stroke{curve, {}, {}};
    stroke.press(0.1, 0.4f);
    stroke.drag(0.5, 0.8f);
    EXPECT_EQ(curve.getDrawGesture().result().peaks, 0);
    EXPECT_NEAR(curve.getDrawGesture().result().cycles, 4.0, 1e-6);
    stroke.release();
}

// The Harmonics Root follows the song key's root until a note is picked.
namespace {
const synth::MusicalScale kDMinor{2, 0x5AD, "Minor"};
const synth::MusicalScale kGMajor{7, 0xAB5, "Major"};

/** A song key the test can change, wired to the Filter's adapter like the card wires the transport's. */
struct SongKeyBox {
    std::optional<synth::MusicalScale> key;
    void wire(ShapeRig& rig) {
        rig.adapter->setSongKeySource([this] { return key; });
    }
};

double songRootHz(int pitchClass) { return pitchgrid::noteHz(36 + pitchClass); }
} // namespace

TEST(ShapeWindowSongKeyRoot, HarmonicsFollowTheSongKeyRootWhileTheWindowIsOpen) {
    PitchKeysGuard keys;
    ShapeRig rig;
    SongKeyBox song;
    song.key = kDMinor;
    song.wire(rig);
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(choiceOf(pitchgrid::Kind::harmonics));
    EXPECT_EQ(rows.getRootBox().getSelectedId(), PitchGridRows::kRootSongKey);
    EXPECT_EQ(rows.getRootBox().getText(), "Song key root");
    EXPECT_NEAR(rows.rootHz(), 73.416, 0.01) << "D minor: D2";
    auto grid = rows.grid();
    ASSERT_TRUE(grid.has_value());
    EXPECT_NE(lineAt(*grid, songRootHz(2) * 3), nullptr) << "the lines are D2's harmonics";
    EXPECT_EQ(lineAt(*grid, 110.0 * 3), nullptr) << "not A2's";

    int changes = 0;
    rows.onGridChanged = [&changes] { ++changes; };
    song.key = kGMajor;
    rows.poll();
    EXPECT_EQ(changes, 1) << "a song key change re-lays the lines";
    EXPECT_NEAR(rows.rootHz(), songRootHz(7), 1e-6);
    grid = rows.grid();
    EXPECT_NE(lineAt(*grid, songRootHz(7) * 3), nullptr);
    EXPECT_EQ(lineAt(*grid, songRootHz(2) * 3), nullptr) << "D2's lines are gone";
    rows.poll();
    EXPECT_EQ(changes, 1) << "no change, no re-lay";
}

TEST(ShapeWindowSongKeyRoot, OneSongKeyChangeMovesTheNotesKeyAndTheHarmonicsRootTogether) {
    PitchKeysGuard keys;
    ShapeRig rig;
    SongKeyBox song;
    song.key = kCMajor;
    song.wire(rig);
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(choiceOf(pitchgrid::Kind::notes));
    ASSERT_TRUE(rows.activeKey().has_value());
    EXPECT_EQ(rows.activeKey()->rootPitchClass, 0);
    EXPECT_NEAR(rows.rootHz(), songRootHz(0), 1e-6);

    song.key = kDMinor;
    rows.poll();
    // Both rows read the one song key after the one change, whichever grid kind is showing.
    ASSERT_TRUE(rows.activeKey().has_value());
    EXPECT_EQ(rows.activeKey()->rootPitchClass, 2) << "the Notes Key field followed";
    EXPECT_NEAR(rows.rootHz(), songRootHz(2), 1e-6) << "the Harmonics Root followed";
    EXPECT_TRUE(rows.getKeyField().isFollowing());
    EXPECT_TRUE(synth::sameKey(rows.getKeyField().getKey(), song.key)) << "the field itself shows D minor";
    rows.setChoice(choiceOf(pitchgrid::Kind::harmonics));
    EXPECT_NE(lineAt(*rows.grid(), songRootHz(2) * 2), nullptr);
}

TEST(ShapeWindowSongKeyRoot, APickedNoteStaysWhenTheSongKeyChangesAndSongKeyRootFollowsAgain) {
    PitchKeysGuard keys;
    ShapeRig rig;
    SongKeyBox song;
    song.key = kDMinor;
    song.wire(rig);
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(choiceOf(pitchgrid::Kind::harmonics));
    rows.setRootId(PitchGridRows::rootIdOf(48)); // C3
    int changes = 0;
    rows.onGridChanged = [&changes] { ++changes; };
    song.key = kGMajor;
    rows.poll();
    EXPECT_EQ(changes, 0) << "the lines do not move";
    EXPECT_NEAR(rows.rootHz(), pitchgrid::noteHz(48), 1e-6);
    EXPECT_EQ(rows.getRootBox().getSelectedId(), PitchGridRows::rootIdOf(48));

    rows.setRootId(PitchGridRows::kRootSongKey);
    EXPECT_NEAR(rows.rootHz(), songRootHz(7), 1e-6) << "G now";
    song.key = kDMinor;
    rows.poll();
    EXPECT_NEAR(rows.rootHz(), songRootHz(2), 1e-6) << "following again";
}

TEST(ShapeWindowSongKeyRoot, TheRootChoiceSurvivesReopeningTheWindow) {
    PitchKeysGuard keys;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    ShapeRig rig;
    rig.adapter->setApplicationProperties(&props);
    SongKeyBox song;
    song.key = kDMinor;
    song.wire(rig);
    {
        ShapeListEditor editor(rig.adapter, 0);
        editor.getPitchRows()->setChoice(choiceOf(pitchgrid::Kind::harmonics));
        EXPECT_EQ(editor.getPitchRows()->getRootBox().getSelectedId(), PitchGridRows::kRootSongKey);
    }
    song.key = kGMajor; // the key changes while the window is closed
    {
        ShapeListEditor again(rig.adapter, 0);
        EXPECT_EQ(again.getPitchRows()->getChoice().kind, pitchgrid::Kind::harmonics);
        EXPECT_EQ(again.getPitchRows()->getRootBox().getSelectedId(), PitchGridRows::kRootSongKey);
        EXPECT_NEAR(again.getPitchRows()->rootHz(), songRootHz(7), 1e-6) << "still follows, now at G";
        again.getPitchRows()->setRootId(PitchGridRows::rootIdOf(50)); // D3
    }
    song.key = kCMajor;
    ShapeListEditor third(rig.adapter, 0);
    EXPECT_EQ(third.getPitchRows()->getRootBox().getSelectedId(), PitchGridRows::rootIdOf(50));
    EXPECT_NEAR(third.getPitchRows()->rootHz(), pitchgrid::noteHz(50), 1e-6) << "the picked note is kept";
}

TEST(ShapeWindowSongKeyRoot, WithNoSongKeySourceSongKeyRootIsA2AndTheLfoWindowHasNoRootBox) {
    PitchKeysGuard keys;
    ShapeRig rig; // no song key source wired
    ShapeListEditor editor(rig.adapter, 0);
    editor.getPitchRows()->setChoice(choiceOf(pitchgrid::Kind::harmonics));
    EXPECT_NEAR(editor.getPitchRows()->rootHz(), 110.0, 1e-6);

    AudioEngine engine;
    auto* lfo = new LFOModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
    AppUndoManager undo;
    auto lfoAdapter = std::make_shared<LfoShapesAdapter>(*lfo, &undo, engine.getGraph(), node->nodeID, "LFO");
    EXPECT_FALSE(lfoAdapter->getSongKey().has_value());
    ShapeListEditor lfoEditor(lfoAdapter, 0);
    EXPECT_EQ(lfoEditor.getPitchRows(), nullptr) << "the LFO's grid is note lengths: no Root";
}
