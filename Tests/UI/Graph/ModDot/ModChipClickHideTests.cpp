// The mod hover chip under a knob is one fade (ModChipFade, a ShownFade): hover in rises, hover out falls,
// and a click on the knob's dot, which opens the sources panel, makes it fall at once and keeps it down until the
// panel closes (Esc, click away), when it rises again with the pointer still on the knob. The chip's alpha is read
// per frame under Full, Reduce Motion and Animations Off; each stretch is one rise or one fall, never a pop.

#include "../../Layout/PopupAlphaTrace.h"
#include "ModDotTestFixture.h"
#include "UI/Graph/ModuleComponent/ModChipFade.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace {

using synth::test::isNonDecreasing;
using synth::test::isNonIncreasing;
using synth::ui::AnimationMode;

struct ChipModeGuard {
    explicit ChipModeGuard(AnimationMode mode) {
        synth::ui::ShownFade::setAnimateOffScreenForTest(true);
        synth::ui::setAnimationMode(mode);
    }
    ~ChipModeGuard() {
        synth::ui::ShownFade::setAnimateOffScreenForTest(false);
        synth::ui::setAnimationMode(AnimationMode::followSystem);
    }
};

float chipAlpha(ModuleComponent& card) {
    juce::DynamicObject state;
    card.addTestModeState(state);
    return static_cast<float>(static_cast<double>(state.getProperty("modHoverChipAlpha")));
}

// One frame of the app: the card paints (which is when the chip follows what it should show), then the alpha is read.
float paintedAlpha(ModuleComponent& card) {
    juce::Image image(juce::Image::ARGB, card.getWidth(), card.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    card.paintEntireComponent(g, true);
    return chipAlpha(card);
}

// The alphas of `frames` frames, 16 ms apart, from the moment the chip's fade started.
std::vector<float> frames(ModuleComponent& card, int count) {
    std::vector<float> out;
    for (int i = 1; i <= count; ++i) {
        card.getModRingFades().chip().stepForTest(16.0 * i);
        out.push_back(paintedAlpha(card));
    }
    return out;
}

struct ChipTrace {
    std::vector<float> hoverIn, click, panelOpen, panelClosed;
    int repaintsOnClick = 0, repaintsOnClose = 0; // card repaints asked by the panel opening / closing
};

// Hover the knob, let the chip arrive, click the dot, hold with the panel open, close the panel.
ChipTrace traceClick(Fixture& f, synth::theme::AppLookAndFeel& lnf) {
    f.vcaCard->setLookAndFeel(&lnf);
    ChipTrace t;
    paintedAlpha(*f.vcaCard);
    f.editor->setHoveredModTarget(graph_editor_types::HoveredModTarget{f.vcaId, f.gainChannel});
    t.hoverIn.push_back(paintedAlpha(*f.vcaCard));
    for (float a : frames(*f.vcaCard, 14))
        t.hoverIn.push_back(a);

    // The click repaints the card by itself: no paint is asked for here, only read when the card next paints.
    int repaints = 0;
    f.editor->getModDot().onCardRepaint = [&](juce::AudioProcessorGraph::NodeID card) { repaints += card == f.vcaId; };
    f.clickDot();
    t.repaintsOnClick = repaints;
    repaints = 0;
    t.click.push_back(paintedAlpha(*f.vcaCard));
    for (float a : frames(*f.vcaCard, 10))
        t.click.push_back(a);
    t.panelOpen.push_back(paintedAlpha(*f.vcaCard));

    f.held.reset(); // Esc or a click away: the panel is gone, the pointer is still on the knob
    t.repaintsOnClose = repaints;
    t.panelClosed.push_back(paintedAlpha(*f.vcaCard));
    for (float a : frames(*f.vcaCard, 14))
        t.panelClosed.push_back(a);
    f.editor->getModDot().onCardRepaint = nullptr;
    f.vcaCard->setLookAndFeel(nullptr);
    return t;
}

} // namespace

TEST_F(ModuleComponentTest, ChipRisesOnHoverFallsOnClickStaysDownWhilePanelOpenAndRisesOnClose_Full) {
    ChipModeGuard guard(AnimationMode::full);
    Fixture f;
    synth::theme::AppLookAndFeel lnf;
    const auto t = traceClick(f, lnf);
    EXPECT_GE(t.repaintsOnClick, 1) << "the click repaints the card, so the chip starts to fall on its own";
    EXPECT_GE(t.repaintsOnClose, 1) << "closing the panel repaints the card, so the chip rises on its own";
    EXPECT_TRUE(isNonDecreasing(t.hoverIn));
    EXPECT_LT(t.hoverIn.front(), 1.0f) << "no pop in";
    EXPECT_FLOAT_EQ(t.hoverIn.back(), 1.0f);
    EXPECT_TRUE(isNonIncreasing(t.click)) << "one fall";
    EXPECT_LT(t.click[1], 1.0f) << "the fall is under way a frame after the click, without another hover change";
    EXPECT_GT(t.click[1], 0.0f) << "a fall, not a cut";
    EXPECT_FLOAT_EQ(t.click.back(), 0.0f);
    EXPECT_FLOAT_EQ(t.panelOpen.back(), 0.0f) << "hidden while the panel is open";
    EXPECT_TRUE(isNonDecreasing(t.panelClosed)) << "one rise";
    EXPECT_LT(t.panelClosed.front(), 1.0f);
    EXPECT_FLOAT_EQ(t.panelClosed.back(), 1.0f);
}

TEST_F(ModuleComponentTest, ChipClickHideIsAShortFadeUnderReduceMotion) {
    ChipModeGuard guard(AnimationMode::reduced);
    Fixture f;
    synth::theme::AppLookAndFeel lnf;
    const auto t = traceClick(f, lnf);
    EXPECT_TRUE(isNonDecreasing(t.hoverIn));
    EXPECT_FLOAT_EQ(t.hoverIn.back(), 1.0f);
    EXPECT_TRUE(isNonIncreasing(t.click));
    EXPECT_FLOAT_EQ(t.click.back(), 0.0f);
    EXPECT_FLOAT_EQ(t.panelOpen.back(), 0.0f);
    EXPECT_TRUE(isNonDecreasing(t.panelClosed));
    EXPECT_FLOAT_EQ(t.panelClosed.back(), 1.0f);
}

TEST_F(ModuleComponentTest, ChipClickHideIsInstantWithAnimationsOff) {
    ChipModeGuard guard(AnimationMode::off);
    Fixture f;
    synth::theme::AppLookAndFeel lnf;
    const auto t = traceClick(f, lnf);
    EXPECT_FLOAT_EQ(t.hoverIn.front(), 1.0f);
    EXPECT_FLOAT_EQ(t.click.front(), 0.0f) << "gone on the click's own frame";
    EXPECT_FLOAT_EQ(t.panelOpen.back(), 0.0f);
    EXPECT_FLOAT_EQ(t.panelClosed.front(), 1.0f);
}
