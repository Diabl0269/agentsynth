// MixerEqThumbnailTests.cpp -- FRO16 (P9-10, docs/mixer/panel.md#what-the-mixer-shows): the mixer column's EQ curve
// thumbnail. Covers the ticket's own test list -- hidden with no EQ, visible with an enabled
// band, dark/light PNG render showing a boost/cut difference, dimmed when bypassed, and the
// recompute-only-on-parameter-change discipline (root CLAUDE.md "No unconditional per-tick
// repaint") -- plus the click-forwards-to-onClicked seam MixerColumnComponent binds to.
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/ParametricEQModule.h"
#include "UI/Mixer/MixerEqThumbnail.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/Theme.h"
#include <gtest/gtest.h>

namespace {

bool imagesHaveIdenticalPixels(const juce::Image& a, const juce::Image& b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
        return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

/** Renders `thumbnail` (already sized and bound) under the named built-in theme. */
juce::Image renderUnderTheme(synth::ui::MixerEqThumbnail& thumbnail, const juce::String& themeName) {
    synth::theme::AppLookAndFeel laf;
    for (const auto& t : synth::theme::builtInThemes()) {
        if (t.name != themeName)
            continue;
        laf.applyTheme(t);
        thumbnail.setLookAndFeel(&laf);
        const auto img = thumbnail.createComponentSnapshot(thumbnail.getLocalBounds());
        thumbnail.setLookAndFeel(nullptr);
        return img;
    }
    return {};
}

/** A band boosted +12 dB at 1 kHz -- enough to move the curve well away from flat. */
void enableBoostBand(ParametricEQModule& eq) {
    eq.setBandFreq(1, 1000.0f);
    eq.setBandGain(1, 12.0f);
    eq.setBandEnabled(1, true);
}

} // namespace

TEST(MixerEqThumbnailTests, HiddenWithNoEqModule) {
    synth::ui::MixerEqThumbnail thumbnail;
    thumbnail.setSize(140, 28);
    EXPECT_FALSE(thumbnail.isVisible());
}

TEST(MixerEqThumbnailTests, VisibleWithEnabledBand) {
    ParametricEQModule eq;
    enableBoostBand(eq);

    synth::ui::MixerEqThumbnail thumbnail;
    thumbnail.setSize(140, 28);
    thumbnail.setEqModule(&eq);

    EXPECT_TRUE(thumbnail.isVisible());

    thumbnail.setEqModule(nullptr);
}

TEST(MixerEqThumbnailTests, PngRenderDarkThemeShowsBoostCutDifference) {
    ParametricEQModule flatEq;

    synth::ui::MixerEqThumbnail flatThumbnail;
    flatThumbnail.setSize(140, 28);
    flatThumbnail.setEqModule(&flatEq);
    const auto flatImage = renderUnderTheme(flatThumbnail, "Obsidian Studio");

    ParametricEQModule boostedEq;
    enableBoostBand(boostedEq);
    synth::ui::MixerEqThumbnail boostedThumbnail;
    boostedThumbnail.setSize(140, 28);
    boostedThumbnail.setEqModule(&boostedEq);
    const auto boostedImage = renderUnderTheme(boostedThumbnail, "Obsidian Studio");

    ASSERT_GT(flatImage.getWidth(), 0);
    ASSERT_GT(boostedImage.getWidth(), 0);
    EXPECT_FALSE(imagesHaveIdenticalPixels(flatImage, boostedImage))
        << "a +12 dB boost must visibly change the curve under the dark theme";

    flatThumbnail.setEqModule(nullptr);
    boostedThumbnail.setEqModule(nullptr);
}

TEST(MixerEqThumbnailTests, PngRenderLightThemeShowsBoostCutDifference) {
    ParametricEQModule flatEq;

    synth::ui::MixerEqThumbnail flatThumbnail;
    flatThumbnail.setSize(140, 28);
    flatThumbnail.setEqModule(&flatEq);
    const auto flatImage = renderUnderTheme(flatThumbnail, "Daylight Studio");

    ParametricEQModule boostedEq;
    enableBoostBand(boostedEq);
    synth::ui::MixerEqThumbnail boostedThumbnail;
    boostedThumbnail.setSize(140, 28);
    boostedThumbnail.setEqModule(&boostedEq);
    const auto boostedImage = renderUnderTheme(boostedThumbnail, "Daylight Studio");

    ASSERT_GT(flatImage.getWidth(), 0);
    ASSERT_GT(boostedImage.getWidth(), 0);
    EXPECT_FALSE(imagesHaveIdenticalPixels(flatImage, boostedImage))
        << "a +12 dB boost must visibly change the curve under the light theme";

    flatThumbnail.setEqModule(nullptr);
    boostedThumbnail.setEqModule(nullptr);
}

TEST(MixerEqThumbnailTests, DimmedWhenBypassed) {
    ParametricEQModule eq;
    enableBoostBand(eq);

    synth::ui::MixerEqThumbnail thumbnail;
    thumbnail.setSize(140, 28);
    thumbnail.setEqModule(&eq);

    const auto activeImage = renderUnderTheme(thumbnail, "Obsidian Studio");

    eq.setBypassed(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

    const auto bypassedImage = renderUnderTheme(thumbnail, "Obsidian Studio");

    ASSERT_GT(activeImage.getWidth(), 0);
    ASSERT_GT(bypassedImage.getWidth(), 0);
    EXPECT_FALSE(imagesHaveIdenticalPixels(activeImage, bypassedImage))
        << "bypass must visibly dim the curve (textDisabled vs accent fill)";

    thumbnail.setEqModule(nullptr);
}

TEST(MixerEqThumbnailTests, RecomputeOnlyOnParameterChange) {
    ParametricEQModule eq;
    enableBoostBand(eq);

    synth::ui::MixerEqThumbnail thumbnail;
    thumbnail.setSize(140, 28);
    thumbnail.setEqModule(&eq);

    const int afterBind = thumbnail.getRecomputeCountForTest();
    EXPECT_EQ(afterBind, 1) << "binding recomputes exactly once, synchronously";

    // Repeated paints alone must never recompute -- paint() only reads the cache.
    juce::Image img(juce::Image::ARGB, 140, 28, true);
    juce::Graphics g(img);
    thumbnail.paint(g);
    thumbnail.paint(g);
    thumbnail.paint(g);
    EXPECT_EQ(thumbnail.getRecomputeCountForTest(), afterBind);

    // One parameter write + a pumped dispatch loop coalesces to exactly one more recompute.
    eq.setBandGain(1, -6.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    EXPECT_EQ(thumbnail.getRecomputeCountForTest(), afterBind + 1);

    thumbnail.paint(g);
    thumbnail.paint(g);
    EXPECT_EQ(thumbnail.getRecomputeCountForTest(), afterBind + 1)
        << "further paints with no new parameter change must not recompute again";

    thumbnail.setEqModule(nullptr);
}

TEST(MixerEqThumbnailTests, ClickFiresOnClicked) {
    ParametricEQModule eq;
    synth::ui::MixerEqThumbnail thumbnail;
    thumbnail.setSize(140, 28);
    thumbnail.setEqModule(&eq);

    bool clicked = false;
    thumbnail.onClicked = [&] { clicked = true; };

    const juce::Point<int> centre(thumbnail.getWidth() / 2, thumbnail.getHeight() / 2);
    thumbnail.mouseUp(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre.toFloat(),
                                       juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f,
                                       0.0f, 0.0f, &thumbnail, &thumbnail, juce::Time::getCurrentTime(),
                                       centre.toFloat(), juce::Time::getCurrentTime(), 1, false));

    EXPECT_TRUE(clicked);

    thumbnail.setEqModule(nullptr);
}

// FRO16 review follow-up: MixerInsertList::removeRow's onBeforeNodeRemoved hook (see
// MixerColumnComponentTests.cpp's own regression test) is not the only way a single node can be
// freed out from under a bound thumbnail -- a canvas "Delete" on the same EQ module's card
// (GraphEditor::requestDeleteModule) is a different graph.removeNode() call site with no
// equivalent pre-removal hook. setEqModule()'s optional graph/nodeId liveness check in
// detachListeners() is the belt-and-braces fix: it must never dereference eq_ once the node it
// came from is already gone, regardless of which caller forgot to unbind first.
TEST(MixerEqThumbnailTests, DoesNotTouchFreedParametersWhenTheNodeWasRemovedWithoutUnbindingFirst) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto node = graph.addNode(std::make_unique<ParametricEQModule>());
    ASSERT_NE(node, nullptr);
    auto* eq = dynamic_cast<ParametricEQModule*>(node->getProcessor());
    ASSERT_NE(eq, nullptr);
    const auto nodeId = node->nodeID;

    synth::ui::MixerEqThumbnail thumbnail;
    thumbnail.setSize(140, 28);
    thumbnail.setEqModule(eq, &graph, nodeId);
    ASSERT_TRUE(thumbnail.isVisible());

    // Free the node WITHOUT unbinding first -- exactly what a caller with no pre-removal hook
    // does.
    graph.removeNode(nodeId);

    // No crash/UAF is the point: this must not dereference the now-freed ParametricEQModule*.
    thumbnail.setEqModule(nullptr);
    EXPECT_FALSE(thumbnail.isVisible());
}
