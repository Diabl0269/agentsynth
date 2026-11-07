// The mod hover chip fades like every other tooltip (160 ms in, 110 ms out, the last rows kept through the fade-out)
// and is drawn on the tooltip's own box.

#include "ModDotTestFixture.h"
#include "UI/Graph/ModuleComponent/ModuleComponentModChip.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"

namespace {

using synth::ui::ModChipFade;
using synth::ui::ModChipStack;

// Lets a fade on an off-screen host run (stepped by hand) under the given animation mode.
struct OffScreenFades {
    explicit OffScreenFades(synth::ui::AnimationMode mode = synth::ui::AnimationMode::full) {
        synth::ui::ShownFade::setAnimateOffScreenForTest(true);
        synth::ui::setAnimationMode(mode);
    }
    ~OffScreenFades() {
        synth::ui::ShownFade::setAnimateOffScreenForTest(false);
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
    }
};

ModChipStack someStack() {
    ModChipStack stack;
    stack.rows.push_back({"LFO 1 \xC2\xB7 +50%", false, true, false});
    stack.sliderIndex = 0;
    return stack;
}

// The alphas a fade hands out at each of `count` steps of `stepMs` from its start.
std::vector<float> alphasOver(ModChipFade& fade, const ModChipStack& now, double stepMs, int count) {
    std::vector<float> alphas;
    for (int i = 1; i <= count; ++i) {
        fade.stepForTest(stepMs * i);
        alphas.push_back(fade.follow(now).alpha);
    }
    return alphas;
}

juce::Image render(juce::Component& card) {
    juce::Image image(juce::Image::ARGB, card.getWidth(), card.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    card.paintEntireComponent(g, true);
    return image;
}

float chipAlphaOf(ModuleComponent& card) {
    juce::DynamicObject state;
    card.addTestModeState(state);
    return static_cast<float>(static_cast<double>(state.getProperty("modHoverChipAlpha")));
}

} // namespace

// Regression test for FRO1567: the chip appeared at full opacity and vanished the moment the hover ended.
TEST(ModChipFade, ComesInFromPartialAndGoesOutThroughPartialKeepingItsRows) {
    OffScreenFades guard;
    juce::Component host;
    ModChipFade fade(host);
    const ModChipStack none;

    const auto shown = someStack();
    EXPECT_LT(fade.follow(shown).alpha, 1.0f) << "starts below full";
    const auto in = alphasOver(fade, shown, 20.0, 8);
    EXPECT_GT(in.front(), 0.0f);
    EXPECT_LT(in.front(), 1.0f);
    for (size_t i = 1; i < in.size(); ++i)
        EXPECT_GE(in[i], in[i - 1]) << "step " << i;
    EXPECT_FLOAT_EQ(in.back(), 1.0f) << "fully in after 160 ms";

    const auto first = fade.follow(none);
    EXPECT_GT(first.alpha, 0.0f);
    EXPECT_EQ(first.rows.size(), 1u) << "the last rows stay while it fades out";
    const auto out = alphasOver(fade, none, 10.0, 12);
    EXPECT_LT(out.front(), 1.0f);
    for (size_t i = 1; i < out.size(); ++i)
        EXPECT_LE(out[i], out[i - 1]) << "step " << i;
    EXPECT_FLOAT_EQ(out.back(), 0.0f) << "gone after 110 ms";
    EXPECT_TRUE(fade.follow(none).rows.empty());
}

TEST(ModChipFade, AnimationsOffIsInstantAndReduceMotionIsAShortFade) {
    juce::Component host;
    {
        OffScreenFades off(synth::ui::AnimationMode::off);
        ModChipFade fade(host);
        EXPECT_FLOAT_EQ(fade.follow(someStack()).alpha, 1.0f);
        const auto gone = fade.follow(ModChipStack{});
        EXPECT_FLOAT_EQ(gone.alpha, 0.0f);
        EXPECT_TRUE(gone.rows.empty());
    }
    OffScreenFades reduced(synth::ui::AnimationMode::reduced);
    ModChipFade fade(host);
    EXPECT_LT(fade.follow(someStack()).alpha, 1.0f);
    fade.stepForTest(synth::ui::ShownFade::kReducedMs * 0.5);
    const float mid = fade.follow(someStack()).alpha;
    EXPECT_GT(mid, 0.0f);
    EXPECT_LT(mid, 1.0f);
    fade.stepForTest(synth::ui::ShownFade::kReducedMs + 1.0);
    EXPECT_FLOAT_EQ(fade.follow(someStack()).alpha, 1.0f);
}

TEST_F(ModuleComponentTest, TheCardsChipFadesInOnHoverStartAndOutOnHoverEnd) {
    OffScreenFades guard;
    Fixture f;
    synth::theme::AppLookAndFeel lnf;
    f.vcaCard->setLookAndFeel(&lnf);
    render(*f.vcaCard);
    EXPECT_FLOAT_EQ(chipAlphaOf(*f.vcaCard), 0.0f);

    f.editor->setHoveredModTarget(graph_editor_types::HoveredModTarget{f.vcaId, f.gainChannel});
    render(*f.vcaCard);
    EXPECT_LT(chipAlphaOf(*f.vcaCard), 1.0f) << "hover start";
    f.vcaCard->getModRingFades().chip().stepForTest(1000.0);
    render(*f.vcaCard);
    EXPECT_FLOAT_EQ(chipAlphaOf(*f.vcaCard), 1.0f);

    f.editor->setHoveredModTarget(std::nullopt);
    render(*f.vcaCard);
    f.vcaCard->getModRingFades().chip().stepForTest(30.0);
    const float mid = chipAlphaOf(*f.vcaCard);
    EXPECT_GT(mid, 0.0f) << "the chip is still there just after the hover ended";
    EXPECT_LT(mid, 1.0f);
    f.vcaCard->getModRingFades().chip().stepForTest(1000.0);
    EXPECT_FLOAT_EQ(chipAlphaOf(*f.vcaCard), 0.0f);
    f.vcaCard->setLookAndFeel(nullptr);
}

// The chip's box is the tooltip's box: same fill and border, in a dark and a light theme.
TEST_F(ModuleComponentTest, TheChipBoxIsPaintedLikeATooltipBox) {
    OffScreenFades instant(synth::ui::AnimationMode::off); // the chip is at full opacity on its first paint
    for (const auto& theme : {synth::theme::makeObsidian(), synth::theme::makeDaylight()}) {
        Fixture f;
        synth::theme::AppLookAndFeel lnf;
        lnf.applyTheme(theme);
        f.vcaCard->setLookAndFeel(&lnf);
        f.editor->setHoveredModTarget(graph_editor_types::HoveredModTarget{f.vcaId, f.gainChannel});
        const auto card = render(*f.vcaCard);

        juce::DynamicObject state;
        f.vcaCard->addTestModeState(state);
        const auto* rects = state.getProperty("modHoverChipRects").getArray();
        ASSERT_NE(rects, nullptr);
        ASSERT_FALSE(rects->isEmpty());
        const auto box = juce::Rectangle<float>::fromString(rects->getReference(0).toString());
        ASSERT_FALSE(box.isEmpty());

        const int w = juce::roundToInt(box.getWidth());
        const int h = juce::roundToInt(box.getHeight());
        juce::Image tip(juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        {
            juce::Graphics g(tip);
            lnf.drawTooltip(g, {}, w, h);
        }
        const int x0 = juce::roundToInt(box.getX());
        const int y0 = juce::roundToInt(box.getY());
        const int midY = h / 2;
        // Fill, clear of the swatch and the text.
        EXPECT_EQ(card.getPixelAt(x0 + w - 3, y0 + midY), tip.getPixelAt(w - 3, midY)) << theme.id;
        EXPECT_EQ(card.getPixelAt(x0 + w - 3, y0 + midY).getAlpha(), 255) << "opaque: labels under it never show";
        // Border: the left edge column.
        const auto edge = card.getPixelAt(x0, y0 + midY);
        const auto want = tip.getPixelAt(0, midY);
        EXPECT_NEAR(edge.getRed(), want.getRed(), 6) << theme.id;
        EXPECT_NEAR(edge.getGreen(), want.getGreen(), 6) << theme.id;
        EXPECT_NEAR(edge.getBlue(), want.getBlue(), 6) << theme.id;
        f.vcaCard->setLookAndFeel(nullptr);
    }
}
