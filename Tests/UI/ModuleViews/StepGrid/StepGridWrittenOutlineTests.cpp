// StepGridWrittenOutlineTests.cpp -- a step a scale moved shows the note it was written at as a solid outline in the
// text colour that stands out from the row on a dark and a light theme, from the moment the scale changes, and goes
// when the scale does.
#include "StepGridCardFixture.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"

using stepgrid_test::SeqCard;

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
