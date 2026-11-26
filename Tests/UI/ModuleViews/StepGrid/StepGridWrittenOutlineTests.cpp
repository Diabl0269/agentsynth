// StepGridWrittenOutlineTests.cpp -- a step a scale moved shows the note it was written at as a solid outline in the
// text colour that stands out from the row on a dark and a light theme, from the moment the scale changes, and goes
// when the scale does. It also holds the undo-glide registry fixtures for the Sequencer grid's bars (a Key pick, a
// note dragged in the Pitch lane), here so no new unit widens the reach of the card-body headers.
#include "../../Layout/UndoGlideSurfaces.h"
#include "Music/MusicalScale.h"
#include "Music/SongKey.h"
#include "StepGridCardFixture.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/ModuleViews/SequencerKey/SequencerKeyField.h"
#include "UI/ModuleViews/StepGrid/StepGridChords.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <cmath>

using stepgrid_test::SeqCard;
using Tool = synth::ui::StepGridTools::Tool;

namespace {

juce::Image render(juce::Component& c) {
    juce::Image img(juce::Image::ARGB, std::max(1, c.getWidth()), std::max(1, c.getHeight()), true,
                    juce::SoftwareImageType());
    juce::Graphics g(img);
    c.paintEntireComponent(g, false);
    return img;
}

void writeE(SeqCard& c) {
    auto pattern = c.module->getPattern();
    pattern.steps[0].pitch = 64; // E
    c.module->setPattern(pattern);
    c.grid->poll();
}

float distance(juce::Colour a, juce::Colour b) {
    return std::abs(a.getFloatRed() - b.getFloatRed()) + std::abs(a.getFloatGreen() - b.getFloatGreen()) +
           std::abs(a.getFloatBlue() - b.getFloatBlue());
}

} // namespace

// Regression test for FRO1037: the written note's outline was a 1 px muted dash, nearly invisible.
TEST(StepGridWrittenOutline, AScaleThatMovesTheNoteDrawsTheWrittenENoteInTheTextColourOnBothThemes) {
    for (const auto& theme : {synth::theme::makeObsidian(), synth::theme::makeDaylight()}) {
        SeqCard c;
        synth::theme::AppLookAndFeel laf;
        laf.applyTheme(theme);
        c.grid->setLookAndFeel(&laf);
        c.useOwnKey();
        c.setChoice("scaleRoot", "C");
        c.setChoice("scale", "No scale");
        writeE(c);
        EXPECT_TRUE(c.grid->writtenOutlineBounds(0).isEmpty()) << theme.name << ": no scale, nothing moved";

        c.setChoice("scale", "Natural Minor");
        c.grid->poll(); // the card tick: the very next poll after the change, no hover needed
        ASSERT_EQ(c.grid->playedPitchOf(0), 63) << "E in C Natural Minor plays D sharp";
        const auto outline = c.grid->writtenOutlineBounds(0);
        ASSERT_FALSE(outline.isEmpty()) << theme.name;

        const auto image = render(*c.grid);
        const int x = juce::roundToInt(outline.getCentreX());
        const int y = juce::roundToInt(outline.getY()); // inside the 1.5 px top edge
        const auto edge = image.getPixelAt(x, y);
        const auto row = image.getPixelAt(x, juce::roundToInt(outline.getCentreY()));
        EXPECT_LT(distance(edge, theme.colors.textPrimary), 0.25f) << theme.name << ": the edge is the text colour";
        EXPECT_GT(distance(edge, row), 0.6f) << theme.name << ": the edge stands out from the row it is on";

        c.setChoice("scale", "No scale");
        c.grid->poll();
        EXPECT_TRUE(c.grid->writtenOutlineBounds(0).isEmpty()) << theme.name << ": no scale, the outline goes";
        c.grid->setLookAndFeel(nullptr);
    }
}

TEST(StepGridWrittenOutline, TheWrittenRowIsAlwaysInTheShownRange) {
    SeqCard c;
    c.useOwnKey();
    c.setChoice("scaleRoot", "C");
    c.setChoice("scale", "Natural Minor");
    auto pattern = c.module->getPattern();
    for (int i = 0; i < 4; ++i)
        pattern.steps[(size_t)i].pitch = 64 + i * 7; // spread wide so the range has to grow to fit them
    c.module->setPattern(pattern);
    c.grid->poll();
    for (int i = 0; i < 4; ++i)
        if (c.grid->playedPitchOf(i) != pattern.steps[(size_t)i].pitch)
            EXPECT_FALSE(c.grid->writtenOutlineBounds(i).isEmpty()) << "step " << i;
}

// Regression test for FRO1037: the note name above a moved bar was drawn across the outline of the written note.
TEST(StepGridWrittenOutline, TheNoteNameNeverCrossesTheOutline) {
    SeqCard c("Poly Sequencer");
    c.useOwnKey();
    c.setChoice("scaleRoot", "C");
    c.setChoice("scale", "Natural Minor");
    writeE(c);
    ASSERT_FALSE(c.grid->writtenOutlineBounds(0).isEmpty());
    const auto label = c.grid->chordLabelBounds(0).toFloat();
    ASSERT_FALSE(label.isEmpty());
    EXPECT_FALSE(label.intersects(c.grid->writtenOutlineBounds(0)));
}

namespace {

// E written on step 1, played through C Natural Minor (so it plays D sharp and the outline marks E), motion on.
void scaledCardWithE(SeqCard& c) {
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    c.useOwnKey();
    c.setChoice("scaleRoot", "C");
    c.setChoice("scale", "Natural Minor");
    writeE(c);
    c.grid->poll();
    c.grid->finishMotionForTest();
}

} // namespace

// the outline used to be skipped for the whole span of a glide and pop back on its last frame.
TEST(StepGridWrittenOutline, AnUndoThatGlidesTheBarsKeepsTheOutlineOfANoteThatDidNotMoveOnScreen) {
    SeqCard c;
    scaledCardWithE(c);
    ASSERT_FALSE(c.grid->writtenOutlineBounds(0).isEmpty());
    auto pattern = c.module->getPattern();
    pattern.steps[1].pitch = 72; // what an undo of another step looks like: a new pattern, step 1's bar glides
    c.module->setPattern(pattern);
    c.grid->poll();
    ASSERT_TRUE(c.grid->isMotionRunning());
    for (const double ms : {0.0, 40.0, 80.0, 160.0}) {
        c.grid->stepMotionForTest(ms);
        EXPECT_FALSE(c.grid->writtenOutlineBounds(0).isEmpty()) << "gone " << ms << " ms into the glide";
        EXPECT_FLOAT_EQ(c.grid->writtenOutlineAlpha(0, false), 1.0f) << "one steady outline, no dip";
        EXPECT_FLOAT_EQ(c.grid->writtenOutlineAlpha(0, true), 0.0f);
    }
    c.grid->finishMotionForTest();
    EXPECT_FALSE(c.grid->writtenOutlineBounds(0).isEmpty());
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(StepGridWrittenOutline, AnUndoThatTakesTheScaleAwayFadesTheOutlineOutAndNeverDropsIt) {
    SeqCard c;
    scaledCardWithE(c);
    auto pattern = c.module->getPattern();
    pattern.steps[1].pitch = 72;
    c.module->setPattern(pattern);
    c.setChoice("scale", "No scale"); // the same undo step: the pattern and the key go back together
    c.grid->poll();
    ASSERT_EQ(c.grid->playedPitchOf(0), 64);
    float last = 1.0f;
    for (const double ms : {0.0, 20.0, 40.0, 60.0}) {
        c.grid->stepMotionForTest(ms);
        const float alpha = c.grid->writtenOutlineAlpha(0, true);
        EXPECT_LE(alpha, last) << "fades out one way";
        if (alpha > 0.0f)
            EXPECT_FALSE(c.grid->writtenOutlineBounds(0).isEmpty()) << "still drawn after " << ms << " ms more";
        last = alpha;
    }
    EXPECT_GT(last, 0.0f) << "mid-glide the old outline is still partly there";
    EXPECT_LT(last, 1.0f);
    EXPECT_FLOAT_EQ(c.grid->writtenOutlineAlpha(0, false), 0.0f)
        << "nothing fades in: the note plays where it was written";
    c.grid->finishMotionForTest();
    EXPECT_TRUE(c.grid->writtenOutlineBounds(0).isEmpty());
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

// Fit to scale glides the bars already; the outline of each note it fitted fades out as its bar arrives.
TEST(StepGridWrittenOutline, FitToScaleFadesTheOutlinesOutWhileTheBarsGlide) {
    SeqCard c;
    scaledCardWithE(c);
    ASSERT_FALSE(c.grid->writtenOutlineBounds(0).isEmpty());
    c.grid->getTools().getButton(Tool::FitToScale).onClick();
    ASSERT_TRUE(c.grid->isMotionRunning());
    c.grid->stepMotionForTest(60.0);
    EXPECT_GT(c.grid->writtenOutlineAlpha(0, true), 0.0f);
    EXPECT_LT(c.grid->writtenOutlineAlpha(0, true), 1.0f) << "partway through the fade";
    c.grid->finishMotionForTest();
    EXPECT_TRUE(c.grid->writtenOutlineBounds(0).isEmpty()) << "written == played now: no outline";
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

// A key change moves every note the steps play: the bars glide from where the old scale put them instead of
// redrawing at once. The same holds for the undo and redo of the change, which land here as the same parameter write.
TEST(StepGridWrittenOutline, ChangingTheKeyGlidesTheBarsAndTheirOutlinesInsteadOfRedrawingAtOnce) {
    SeqCard c;
    scaledCardWithE(c);
    ASSERT_FALSE(c.grid->isMotionRunning());
    for (const char* scale : {"No scale", "Natural Minor"}) {
        c.setChoice("scale", scale);
        c.grid->poll();
        ASSERT_TRUE(c.grid->isMotionRunning()) << "a key change starts a glide: " << scale;
        c.grid->stepMotionForTest(40.0);
        const float fromSide = c.grid->writtenOutlineAlpha(0, true);
        const float toSide = c.grid->writtenOutlineAlpha(0, false);
        EXPECT_TRUE((fromSide > 0.0f && fromSide < 1.0f) || (toSide > 0.0f && toSide < 1.0f))
            << "partway: the outline is neither where it was nor where it goes, " << scale;
        c.grid->finishMotionForTest();
    }
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(StepGridWrittenOutline, ChangingTheKeyLandsAtOnceWithAnimationsOff) {
    SeqCard c;
    scaledCardWithE(c);
    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
    c.setChoice("scale", "No scale");
    c.grid->poll();
    EXPECT_FALSE(c.grid->isMotionRunning());
    EXPECT_TRUE(c.grid->writtenOutlineBounds(0).isEmpty());
    synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

void setTranspose(SeqCard& c, int semitones) {
    auto* t = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(c.module, synth::sequencer_ids::kTranspose));
    ASSERT_NE(t, nullptr);
    *t = semitones;
}

// Regression test for FRO1673: a Transpose change (the knob, its undo, its redo) moved every bar in one frame.
TEST(StepGridWrittenOutline, ChangingTransposeGlidesTheBarsOnBothSequencers) {
    for (const char* type : {"Sequencer", "Poly Sequencer"}) {
        SeqCard c(type);
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
        c.useOwnKey();
        c.setChoice("scale", "No scale");
        writeE(c);
        c.grid->finishMotionForTest();
        ASSERT_FALSE(c.grid->isMotionRunning());
        for (const int semitones : {5, 0}) {
            setTranspose(c, semitones);
            c.grid->poll();
            ASSERT_TRUE(c.grid->isMotionRunning()) << type << " transpose " << semitones;
            c.grid->finishMotionForTest();
            EXPECT_EQ(c.grid->playedPitchOf(0), 64 + semitones);
        }
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
    }
}

TEST(StepGridWrittenOutline, ChangingTransposeAndTheKeyInOnePollStartsOneGlide) {
    SeqCard c;
    scaledCardWithE(c);
    setTranspose(c, 2);
    c.setChoice("scale", "No scale");
    c.grid->poll();
    ASSERT_TRUE(c.grid->isMotionRunning());
    c.grid->stepMotionForTest(40.0);
    c.grid->finishMotionForTest();
    EXPECT_FALSE(c.grid->isMotionRunning());
    EXPECT_EQ(c.grid->playedPitchOf(0), 66);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(StepGridWrittenOutline, ChangingTransposeLandsAtOnceWithAnimationsOff) {
    SeqCard c;
    scaledCardWithE(c);
    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
    setTranspose(c, 3);
    c.grid->poll();
    EXPECT_FALSE(c.grid->isMotionRunning());
    synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(StepGridWrittenOutline, AFollowedSongKeyChangeGlidesTheBars) {
    SeqCard c;
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    c.grid->setSongKey(synth::makeScale(0, synth::scalePresetIndex("Natural Minor")));
    c.grid->poll();
    writeE(c);
    c.grid->finishMotionForTest();
    c.grid->setSongKey(synth::makeScale(0, synth::scalePresetIndex("Major")));
    c.grid->poll();
    EXPECT_TRUE(c.grid->isMotionRunning());
    c.grid->finishMotionForTest();
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

// ---- The Pitch lane's row shading fades to the new key with the bars instead of popping. ----
namespace {

constexpr int kE = 64;     // out of C Natural Minor, in D Natural Minor
constexpr int kC = 60;     // the root of C Natural Minor, plain in D Natural Minor
constexpr int kEFlat = 63; // a black key: out of chromatic, in C Natural Minor

bool strictlyBetween(juce::Colour mid, juce::Colour a, juce::Colour b) {
    const float whole = distance(a, b);
    return whole > 0.01f && distance(a, mid) > 0.005f && distance(b, mid) > 0.005f &&
           distance(a, mid) + distance(mid, b) < whole * 1.05f;
}

struct ShadeCard : SeqCard {
    ShadeCard() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
        useOwnKey();
        setChoice("scaleRoot", "C");
        setChoice("scale", "Natural Minor");
        grid->poll();
        grid->finishMotionForTest();
    }
    ~ShadeCard() { synth::ui::FadeVisibility::setAnimateOffScreenForTest(false); }
    void key(const char* root, const char* scale) {
        setChoice("scaleRoot", root);
        setChoice("scale", scale);
        grid->poll();
    }
    juce::Colour row(int pitch) const { return grid->rowColourForTest(pitch); }
    /** The row's colour when the key has settled on (root, scale), then back to where it was. */
    juce::Colour settled(int pitch, const char* root, const char* scale, const char* backRoot, const char* backScale) {
        key(root, scale);
        grid->finishMotionForTest();
        const auto colour = row(pitch);
        key(backRoot, backScale);
        grid->finishMotionForTest();
        return colour;
    }
};

} // namespace

TEST(StepGridKeyShade, ChangingTheKeyFadesARowIntoTheScaleAndTheRootWashOutInsteadOfPopping) {
    for (const int pitch : {kE, kC}) {
        ShadeCard c;
        const auto before = c.row(pitch);
        const auto after = c.settled(pitch, "D", "Natural Minor", "C", "Natural Minor");
        ASSERT_GT(distance(before, after), 0.01f) << "row " << pitch << " looks different in the two keys";
        c.key("D", "Natural Minor");
        ASSERT_TRUE(c.grid->isMotionRunning());
        EXPECT_LT(distance(c.row(pitch), before), 0.005f) << "the first frame is still the old shade, row " << pitch;
        c.grid->stepMotionForTest(60.0);
        EXPECT_TRUE(strictlyBetween(c.row(pitch), before, after)) << "mid-glide, row " << pitch;
        c.grid->finishMotionForTest();
        EXPECT_LT(distance(c.row(pitch), after), 0.005f) << "lands on the new key, row " << pitch;
    }
}

TEST(StepGridKeyShade, TheUndoOfAKeyChangeFadesTheShadeBackAndTwoQuickOnesStayOneWay) {
    ShadeCard c;
    const auto inC = c.row(kE);
    c.key("D", "Natural Minor");
    c.grid->finishMotionForTest();
    const auto inD = c.row(kE);
    c.key("C", "Natural Minor"); // the undo lands as the same parameter write
    c.grid->stepMotionForTest(60.0);
    EXPECT_TRUE(strictlyBetween(c.row(kE), inD, inC)) << "the undo fades, it does not pop";
    c.grid->finishMotionForTest();
    EXPECT_LT(distance(c.row(kE), inC), 0.005f);
}

TEST(StepGridKeyShade, AKeyChangeMidGlideContinuesFromTheShadeDrawnWithNoJump) {
    ShadeCard c;
    const auto inC = c.row(kE);
    const auto inD = c.settled(kE, "D", "Natural Minor", "C", "Natural Minor");
    c.key("D", "Natural Minor");
    c.grid->stepMotionForTest(40.0);
    const auto drawn = c.row(kE);
    ASSERT_TRUE(strictlyBetween(drawn, inC, inD));
    c.key("C", "Natural Minor"); // pressed again before the first fade ended
    ASSERT_TRUE(c.grid->isMotionRunning());
    EXPECT_LT(distance(c.row(kE), drawn), 0.005f) << "the second fade starts at the shade on screen";
    c.grid->stepMotionForTest(20.0);
    EXPECT_LT(distance(c.row(kE), inC), distance(drawn, inC)) << "and heads back for the first key";
    c.grid->finishMotionForTest();
    EXPECT_LT(distance(c.row(kE), inC), 0.005f);
}

TEST(StepGridKeyShade, GoingFromNoScaleToAScaleAlsoFades) {
    ShadeCard c;
    c.key("C", "No scale");
    c.grid->finishMotionForTest();
    const auto chromatic = c.row(kEFlat);
    c.key("C", "Natural Minor");
    c.grid->stepMotionForTest(60.0);
    const auto mid = c.row(kEFlat);
    c.grid->finishMotionForTest();
    EXPECT_TRUE(strictlyBetween(mid, chromatic, c.row(kEFlat)));
}

TEST(StepGridKeyShade, ReduceMotionAndAnimationsOffLandTheShadeAtOnce) {
    for (const auto mode : {synth::ui::AnimationMode::reduced, synth::ui::AnimationMode::off}) {
        ShadeCard c;
        const auto inC = c.row(kE);
        synth::ui::setAnimationMode(mode);
        c.key("D", "Natural Minor");
        const auto now = c.row(kE);
        c.grid->finishMotionForTest();
        EXPECT_LT(distance(now, c.row(kE)), 0.005f) << "the first frame is the final shade";
        EXPECT_GT(distance(now, inC), 0.01f);
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
    }
}

// ---- The registry entry for the Key's bars (UndoGlideSurfaces.h): here because the card fixture is, and CardBody.h
// stays out of the Layout tests' header reach. ----
namespace undo_glide_test {

namespace {

/** Step 1 is written E and the Key goes from No scale to C Natural Minor, so the bar moves from E down to D sharp. */
class SequencerKeySurface : public Surface {
public:
    SequencerKeySurface() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(true); // a headless grid gets no VBlank
        c_.useOwnKey();
        c_.setChoice("scaleRoot", "C");
        c_.setChoice("scale", "No scale");
        auto pattern = c_.module->getPattern();
        pattern.steps[0].pitch = 64; // E
        c_.module->setPattern(pattern);
        tick();
        c_.grid->finishMotionForTest();
        c_.canvas.undo.getUndoManager().clearUndoHistory();
    }
    ~SequencerKeySurface() override { synth::ui::FadeVisibility::setAnimateOffScreenForTest(false); }

    void move() override {
        keyField()->pickOwn(synth::makeScale(0, synth::scalePresetIndex("Natural Minor")));
        tick();
        grid().finishMotionForTest();
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override { return grid().drawnPitchForTest(0) * 10.0; }
    bool gliding() const override { return grid().isMotionRunning(); }
    void stepTo(double elapsedMs) override { grid().stepMotionForTest(elapsedMs); }

private:
    synth::ui::StepGridComponent& grid() const {
        auto* found = dynamic_cast<synth::ui::StepGridComponent*>(
            c_.canvas.card(c_.id)->getCardBody()->findView(synth::CardView::StepGrid));
        jassert(found != nullptr);
        return *found;
    }
    synth::ui::SequencerKeyField* keyField() const {
        return dynamic_cast<synth::ui::SequencerKeyField*>(
            c_.canvas.card(c_.id)->getCardBody()->findWidget("scaleRoot"));
    }
    void tick() const { c_.canvas.card(c_.id)->timerCallback(); }
    bool restore(bool forward) {
        const bool did = forward ? c_.canvas.undo.redo() : c_.canvas.undo.undo();
        tick(); // the card's 15 Hz tick, which is where the grid reads the restored key
        return did;
    }

    mutable SeqCard c_;
};

/** Step 1 sits at C and is dragged up a fourth. The lowest and highest notes sit on other steps, so the pitch range
 *  (and with it every other bar) stays put and the only motion is step 1's bar. */
class StepGridBarsSurface : public Surface {
public:
    StepGridBarsSurface() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(true); // a headless grid gets no VBlank
        c_.useOwnKey();
        c_.setChoice("scale", "No scale");
        auto pattern = c_.module->getPattern();
        for (int i = 0; i < synth::kMaxSequencerSteps; ++i)
            pattern.steps[(size_t)i].pitch = 60;
        pattern.steps[10].pitch = 36;
        pattern.steps[15].pitch = 84;
        c_.module->setPattern(pattern);
        tick();
        c_.grid->finishMotionForTest();
        c_.canvas.undo.getUndoManager().clearUndoHistory();
    }
    ~StepGridBarsSurface() override { synth::ui::FadeVisibility::setAnimateOffScreenForTest(false); }

    void move() override {
        const auto from = c_.grid->pitchBarBoundsForTest(0).getCentre();
        const float x = (float)c_.grid->cellBounds(synth::SequencerLane::Pitch, 0).getCentreX();
        c_.mouse("down", {x, from.y});
        c_.mouse("drag", {x, from.y - 20.0f});
        c_.mouse("up", {x, from.y - 20.0f});
        tick();
        c_.grid->finishMotionForTest();
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override { return -(double)c_.grid->pitchBarBoundsForTest(0).getCentreY(); }
    bool gliding() const override { return c_.grid->isMotionRunning(); }
    void stepTo(double elapsedMs) override { c_.grid->stepMotionForTest(elapsedMs); }
    juce::Component* paintedComponent() override { return c_.grid; }
    bool repaintsWholeComponent() const override { return true; } // the glide calls the grid's own repaint()

private:
    void tick() const { c_.canvas.card(c_.id)->timerCallback(); }
    bool restore(bool forward) {
        const bool did = forward ? c_.canvas.undo.redo() : c_.canvas.undo.undo();
        tick(); // the card's 15 Hz tick, which is where the grid reads the restored pattern
        return did;
    }

    mutable SeqCard c_;
};

} // namespace

std::unique_ptr<Surface> makeSequencerKey() { return std::make_unique<SequencerKeySurface>(); }
std::unique_ptr<Surface> makeStepGridBars() { return std::make_unique<StepGridBarsSurface>(); }

} // namespace undo_glide_test

using synth::SequencerChord;
using synth::ui::StepGridComponent;
using synth::ui::stepgrid::fitChordName;
using synth::ui::stepgrid::kColumns;
using synth::ui::stepgrid::kGutterWidth;

namespace {

constexpr int kNarrowestColumn = 22; // a label is drawn from this column width up
constexpr int kWidestColumn = 80;

int gridWidthForColumn(int column) { return kGutterWidth + 2 + kColumns * column; } // kRightMargin is 2

void sizeGrid(SeqCard& c, int column) {
    c.grid->setSize(gridWidthForColumn(column), c.grid->currentLayoutHeight());
    c.grid->poll();
}

juce::Image renderRing(juce::Component& comp) {
    juce::Image image(juce::Image::ARGB, std::max(1, comp.getWidth()), std::max(1, comp.getHeight()), true,
                      juce::SoftwareImageType());
    juce::Graphics g(image);
    comp.paintEntireComponent(g, false);
    return image;
}

// How much of `accent` the ring put on top of `base` at one pixel: 0 none, 1 all of it.
float coverage(juce::Colour base, juce::Colour ringed, juce::Colour accent) {
    const float v[3] = {accent.getFloatRed() - base.getFloatRed(), accent.getFloatGreen() - base.getFloatGreen(),
                        accent.getFloatBlue() - base.getFloatBlue()};
    const float d[3] = {ringed.getFloatRed() - base.getFloatRed(), ringed.getFloatGreen() - base.getFloatGreen(),
                        ringed.getFloatBlue() - base.getFloatBlue()};
    float vv = 0.0f, dv = 0.0f;
    for (int i = 0; i < 3; ++i) {
        vv += v[i] * v[i];
        dv += d[i] * v[i];
    }
    return vv <= 1.0e-6f ? 0.0f : juce::jlimit(0.0f, 1.0f, dv / vv);
}

// Every step off, so no bar is the accent (a ring over an accent bar would show no difference).
void allStepsOff(SeqCard& c) {
    auto pattern = c.module->getPattern();
    for (auto& step : pattern.steps)
        step.on = false;
    c.module->setPattern(pattern);
    c.grid->poll();
}

struct SideMeasure {
    float inset[4] = {};     // px between the cell's edge and the ring's outer edge: left, top, right, bottom
    float thickness[4] = {}; // the accent coverage summed across the ring on each side
    int ringPixelsOutside = 0;
};

// Walks in from each side of `cell` along its middle and measures the ring that `ringed` added over `base`.
SideMeasure measureRing(const juce::Image& base, const juce::Image& ringed, juce::Rectangle<int> cell,
                        juce::Colour accent) {
    SideMeasure m;
    const auto cover = [&](int x, int y) { return coverage(base.getPixelAt(x, y), ringed.getPixelAt(x, y), accent); };
    const auto differs = [&](int x, int y) { return base.getPixelAt(x, y) != ringed.getPixelAt(x, y); };
    const int midX = cell.getCentreX(), midY = cell.getCentreY();
    for (int side = 0; side < 4; ++side) {
        const int length = side % 2 == 0 ? cell.getWidth() / 2 : cell.getHeight() / 2;
        bool inRing = false;
        for (int i = 0; i < length; ++i) {
            int x = 0, y = 0;
            if (side == 0) {
                x = cell.getX() + i;
                y = midY;
            } else if (side == 1) {
                x = midX;
                y = cell.getY() + i;
            } else if (side == 2) {
                x = cell.getRight() - 1 - i;
                y = midY;
            } else {
                x = midX;
                y = cell.getBottom() - 1 - i;
            }
            if (differs(x, y)) {
                if (!inRing)
                    m.inset[side] = (float)i;
                inRing = true;
                m.thickness[side] += cover(x, y);
            } else if (inRing) {
                break;
            }
        }
        if (!inRing)
            m.inset[side] = -1.0f;
    }
    for (int y = 0; y < base.getHeight(); ++y)
        for (int x = 0; x < base.getWidth(); ++x)
            if (!cell.contains(x, y) && cell.expanded(4).contains(x, y) && differs(x, y))
                ++m.ringPixelsOutside;
    return m;
}

} // namespace

// Guard (kinds layout-overlap, focus-ring-box): The ring on step 1, the last visible step and every
// lane's edge sits wholly inside its cell, one weight all round.
TEST(StepGridFocusRing, TheRingOfTheFirstAndLastVisibleStepOfEveryLaneIsInsideItsCellWithEqualWeightOnAllSides) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        SeqCard c("Poly Sequencer");
        synth::theme::AppLookAndFeel laf;
        laf.applyTheme(theme);
        c.grid->setLookAndFeel(&laf);
        auto* steps = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(c.module, "steps"));
        ASSERT_NE(steps, nullptr);
        *steps = 16;
        auto& chips = c.grid->getChipRow();
        for (int i = 0; i < chips.getNumChips(); ++i)
            chips.getChip(i)->setToggleState(true, juce::sendNotificationSync); // every lane shown
        c.grid->finishMotionForTest();
        sizeGrid(c, 40);
        allStepsOff(c);
        const float want = synth::ui::focusRingThickness(theme);

        int measured = 0;
        for (const int step : {0, 15}) {
            c.press(juce::KeyPress::homeKey);
            for (int i = 0; i < step; ++i)
                c.press(juce::KeyPress::rightKey);
            ASSERT_EQ(c.grid->getFocusedStep(), step);
            for (int lane = 0; lane < synth::kSequencerLaneCount; ++lane) {
                const auto cell = c.grid->cellBounds((SequencerLane)lane, step);
                if (cell.isEmpty())
                    continue;
                // Put the keys on this lane.
                for (int guard = 0;
                     guard < synth::kSequencerLaneCount && c.grid->getFocusedLane() != (SequencerLane)lane; ++guard)
                    c.press(juce::KeyPress::pageDownKey);
                ASSERT_EQ(c.grid->getFocusedLane(), (SequencerLane)lane);

                const auto base = renderRing(*c.grid);
                juce::Image ringed;
                {
                    synth::ui::setFocusRingsShownForTest(true);
                    ringed = renderRing(*c.grid);
                    synth::ui::setFocusRingsShownForTest(false);
                }
                const auto m = measureRing(base, ringed, cell, theme.colors.accent);
                const juce::String where = theme.name + ", lane " + juce::String(lane) + ", step " + juce::String(step);
                EXPECT_EQ(m.ringPixelsOutside, 0) << where << ": no ring pixel outside its cell";
                for (int side = 0; side < 4; ++side) {
                    EXPECT_GE(m.inset[side], 0.0f) << where << ": side " << side << " has a ring";
                    EXPECT_NEAR(m.thickness[side], want, 0.6f)
                        << where << ": side " << side << " has the shared weight";
                }
                // Clear of the lane's edge and the same distance from it on every side.
                const float least = *std::min_element(m.inset, m.inset + 4);
                const float most = *std::max_element(m.inset, m.inset + 4);
                EXPECT_GE(least, 1.0f) << where << ": the ring stands in from the lane's edge";
                EXPECT_LE(most - least, 1.0f) << where << ": the same distance from the edge on all four sides";
                ++measured;
            }
        }
        EXPECT_GE(measured, 10) << "the first and last step of at least five lanes were measured";
        c.grid->setLookAndFeel(nullptr);
    }
}

// Guard: with the keys on a Pitch-lane step of a Poly Sequencer the only mark that differs from rest is the
// column's ring: nothing is drawn on the chord name inside it.
TEST(StepGridFocusRing, AFocusedPolyStepPaintsExactlyOneRingAndNoSecondOutlineOnItsChordName) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        SeqCard c("Poly Sequencer");
        synth::theme::AppLookAndFeel laf;
        laf.applyTheme(theme);
        c.grid->setLookAndFeel(&laf);
        sizeGrid(c, 40);
        allStepsOff(c);
        for (int step = 0; step < 4; ++step) {
            c.press(juce::KeyPress::homeKey);
            for (int i = 0; i < step; ++i)
                c.press(juce::KeyPress::rightKey);
            ASSERT_EQ(c.grid->getFocusedLane(), SequencerLane::Pitch);
            const auto cell = c.grid->cellBounds(SequencerLane::Pitch, step);
            ASSERT_FALSE(c.grid->chordLabelBounds(step).isEmpty());

            const auto base = renderRing(*c.grid);
            synth::ui::setFocusRingsShownForTest(true);
            const auto ringed = renderRing(*c.grid);
            synth::ui::setFocusRingsShownForTest(false);

            // The ring's band: its stroke and the gap the cell's edge leaves; everything inside it must be unchanged.
            const int band = (int)std::ceil(synth::ui::focusRingThickness(theme)) + 3;
            const auto interior = cell.reduced(band);
            int changedInside = 0, changedInBand = 0;
            for (int y = cell.getY(); y < cell.getBottom(); ++y)
                for (int x = cell.getX(); x < cell.getRight(); ++x)
                    if (base.getPixelAt(x, y) != ringed.getPixelAt(x, y))
                        ++(interior.contains(x, y) ? changedInside : changedInBand);
            EXPECT_EQ(changedInside, 0) << theme.name << ", step " << step << ": nothing but the ring changes";
            EXPECT_GT(changedInBand, 0) << theme.name << ", step " << step << ": the ring shows";
        }
        c.grid->setLookAndFeel(nullptr);
    }
}

// Guard (kind layout-overlap): at every column width a chord label's rectangle (its hit area and its pill) lies
// inside its own column, two neighbours never touch, and every chord name, on every root, fits it or is cut to it.
TEST(StepGridFocusRing, EveryChordLabelStaysInsideItsColumnAtEveryColumnWidthAndEveryChordNameFitsIt) {
    SeqCard c("Poly Sequencer");
    auto* steps = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(c.module, "steps"));
    ASSERT_NE(steps, nullptr);
    *steps = 16;

    const synth::MusicalScale chromatic;
    const auto cMajor = synth::makeScale(0, 0);
    std::vector<juce::String> names;
    for (int chord = 0; chord < synth::kSequencerChordCount; ++chord)
        for (int root = 48; root < 60; ++root)
            for (const auto* scale : {&chromatic, &cMajor})
                names.push_back(synth::ui::stepgrid::chordLabelFor((SequencerChord)chord, root, *scale, 7u, 3).name);

    for (int column = kNarrowestColumn; column <= kWidestColumn; ++column) {
        sizeGrid(c, column);
        for (int step = 0; step < kColumns; ++step) {
            const auto label = c.grid->chordLabelBounds(step);
            ASSERT_FALSE(label.isEmpty()) << "column " << column << ", step " << step;
            const int left = kGutterWidth + step * column;
            EXPECT_GE(label.getX(), left) << "column " << column << ", step " << step << ": not left of its column";
            EXPECT_LE(label.getRight(), left + column) << "column " << column << ", step " << step << ": not past it";
            if (step + 1 < kColumns)
                EXPECT_LT(label.getRight(), c.grid->chordLabelBounds(step + 1).getX())
                    << "column " << column << ", step " << step << ": clear of its neighbour";
        }
        const float room = (float)c.grid->chordLabelBounds(0).getWidth();
        for (const auto& name : names) {
            const auto shown = fitChordName(name, room);
            EXPECT_LE((float)synth::theme::AppLookAndFeel::uiTextWidth(shown, 11.0f), room)
                << name << " at column " << column << ": the drawn text fits the label";
            EXPECT_TRUE(shown.isNotEmpty()) << name << " at column " << column << ": something is shown";
            if ((float)synth::theme::AppLookAndFeel::uiTextWidth(name, 11.0f) <= room)
                EXPECT_EQ(shown, name) << name << " at column " << column << ": a name that fits is not cut";
        }
    }
}
