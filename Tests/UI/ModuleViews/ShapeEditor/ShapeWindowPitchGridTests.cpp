// ShapeWindowPitchGridTests.cpp -- the Filter shape window's pitch grids: the Notes and Harmonics lines against note
// frequencies, steps down to a semitone, the separate Level row, the snap switches, the key's out-of-key notes, the
// played-note root, the remembered (and migrated) choice, and the Ring helpers' peaks in the filter's own response.
#include "../../../TestSettingsHelpers.h"
#include "../../Accessibility/TabOrderHelpers.h"
#include "Modules/Filter/CustomFilterBlend.h"
#include "Modules/Filter/CustomFilterShapes.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/ShapeEditor/PitchGrid.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeRingHelpers.h"
#include "UI/ModuleViews/ShapeEditor/ShapeWindowPrefs.h"

using namespace synth::ui;
using namespace synth::ui::test;
using synth::CustomFilterShapes;
using GridLine = CurveEditorComponent::GridLine;

namespace {

const synth::MusicalScale kCMajor{0, 0xAB5, "Major"};
const synth::MusicalScale kAMinor{9, 0x5AD, "Minor"};

struct PitchKeysGuard {
    synth::test::PersistedKeysGuard guard{
        juce::StringArray{shapewindowprefs::gridKey("filter"), shapewindowprefs::gridKindKey("filter"),
                          shapewindowprefs::gridStepsKey("filter"), shapewindowprefs::gridLevelKey("filter"),
                          shapewindowprefs::snapAcrossKey("filter"), shapewindowprefs::snapLevelKey("filter"),
                          shapewindowprefs::gridOwnKeyKey("filter"), shapewindowprefs::gridRootKey("filter")}};
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
    curve.mouseDrag(curveLeftDrag(curve, to, from));
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

TEST(ShapeWindowPitchGrid, SnapAcrossOffLeavesTheFrequencyFreeAndKeepsTheLevelSnap) {
    PitchKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto choice = choiceOf(pitchgrid::Kind::notes);
    choice.snapAcross = false;
    editor.getPitchRows()->setChoice(choice);
    EXPECT_FALSE(editor.getPitchRows()->getSnapAcross().getToggleState());
    auto& curve = editor.getCurveEditor();
    const double x = CustomFilterShapes::hzToX(452.0f);
    dragNode(curve, 3, x, CustomFilterShapes::dbToY(-4.0f));
    const int node = nodeNear(curve, x, 2e-3);
    ASSERT_GE(node, 0) << "452 Hz stays put, between A4 and A#4";
    EXPECT_NEAR(curve.getModel().getNode(node).y, CustomFilterShapes::dbToY(-6.0f), 1e-4) << "the level still snaps";
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
    EXPECT_EQ(changes, 1);
    EXPECT_NEAR(rows.rootHz(), 196.0, 1e-3);
    const auto grid = rows.grid();
    ASSERT_TRUE(grid.has_value());
    EXPECT_NE(lineAt(*grid, 196.0 * 3), nullptr) << "the lines are G3's harmonics";
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
    ASSERT_TRUE(menu.run("Stack harmonics"));
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
    const juce::Component* order[] = {&rows.getKindSwitch(),       &rows.getSnapAcross(),   &rows.getLevelSwitch(),
                                      &rows.getSnapLevel(),        &rows.getRootBox(),      editor.getGuidesToggle(),
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
