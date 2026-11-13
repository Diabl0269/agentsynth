// StepGridWrittenOutlineTests.cpp -- a step a scale moved shows the note it was written at as a solid outline in the
// text colour that stands out from the row on a dark and a light theme, from the moment the scale changes, and goes
// when the scale does.
#include "../../Layout/UndoGlideSurfaces.h"
#include "Music/MusicalScale.h"
#include "Music/SongKey.h"
#include "StepGridCardFixture.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/ModuleViews/SequencerKey/SequencerKeyField.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"

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

} // namespace

std::unique_ptr<Surface> makeSequencerKey() { return std::make_unique<SequencerKeySurface>(); }

} // namespace undo_glide_test
