// CardPanelPictureTests.cpp -- the card panel drawn from a shared picture: the same pixels as drawing it live, one
// render per size/state however many cards use it, and a re-render when the theme or the size changes.
//
// Images use SoftwareImageType(): the default native image type reads back zeros on a Windows runner.

#include "AudioEngine/AudioEngine.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/CardPanelPicture.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>

using synth::theme::DepthLevel;
using synth::ui::CardPanelPicture;

namespace {

constexpr int kW = 260;
constexpr int kH = 120;
constexpr int kHeader = 24;

// Every test leaves the picture on, software-backed and empty, as it found it.
struct PictureGuard {
    PictureGuard() {
        CardPanelPicture::resetForTest();
        CardPanelPicture::setSoftwareImagesForTest(true);
    }
    ~PictureGuard() {
        CardPanelPicture::setEnabledForTest(true);
        CardPanelPicture::setSoftwareImagesForTest(false);
        CardPanelPicture::resetForTest();
    }
};

synth::theme::Theme themeAt(DepthLevel level) {
    auto theme = synth::theme::makeObsidian();
    theme.treatment.depth = level;
    return theme;
}

juce::Image paintPanel(synth::theme::AppLookAndFeel& lf, int w, int h, bool selected, bool bypassed, bool picture,
                       float scale = 1.0f) {
    CardPanelPicture::setEnabledForTest(picture);
    juce::Image image(juce::Image::ARGB, (int)std::ceil((float)w * scale), (int)std::ceil((float)h * scale), true,
                      juce::SoftwareImageType());
    juce::Graphics g(image);
    g.addTransform(juce::AffineTransform::scale(scale));
    CardPanelPicture::draw(g, lf, {w, h}, kHeader, selected, bypassed);
    return image;
}

// Pixels that differ by more than `tolerance` in any channel. Compositing the picture rounds each translucent layer
// once more than drawing the layers straight onto the card, so one or two levels of difference are expected.
int differingPixels(const juce::Image& a, const juce::Image& b, int tolerance = 2) {
    int n = 0;
    for (int y = 0; y < a.getHeight(); ++y) {
        for (int x = 0; x < a.getWidth(); ++x) {
            const auto p = a.getPixelAt(x, y);
            const auto q = b.getPixelAt(x, y);
            if (std::abs(p.getAlpha() - q.getAlpha()) > tolerance || std::abs(p.getRed() - q.getRed()) > tolerance ||
                std::abs(p.getGreen() - q.getGreen()) > tolerance || std::abs(p.getBlue() - q.getBlue()) > tolerance)
                ++n;
        }
    }
    return n;
}

} // namespace

TEST(CardPanelPicture, PaintsTheSamePixelsAsDrawingThePanelLive) {
    PictureGuard guard;
    for (const auto depth : {DepthLevel::Soft, DepthLevel::Rich}) {
        synth::theme::AppLookAndFeel lf;
        lf.applyTheme(themeAt(depth));
        for (const bool selected : {false, true}) {
            for (const bool bypassed : {false, true}) {
                const auto live = paintPanel(lf, kW, kH, selected, bypassed, false);
                const auto pictured = paintPanel(lf, kW, kH, selected, bypassed, true);
                EXPECT_EQ(differingPixels(live, pictured), 0)
                    << "depth " << (int)depth << " selected " << selected << " bypassed " << bypassed;
            }
        }
    }
}

TEST(CardPanelPicture, PaintsTheSamePixelsAtAZoomedScale) {
    PictureGuard guard;
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(themeAt(DepthLevel::Rich));
    const auto live = paintPanel(lf, kW, kH, true, false, false, 2.0f);
    const auto pictured = paintPanel(lf, kW, kH, true, false, true, 2.0f);
    EXPECT_EQ(differingPixels(live, pictured), 0);
}

TEST(CardPanelPicture, ACardPaintsTheSamePixelsWithAndWithoutThePicture) {
    PictureGuard guard;
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule processor;
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(themeAt(DepthLevel::Rich));
    ModuleComponent card(&processor, juce::AudioProcessorGraph::NodeID(1), editor);
    card.setLookAndFeel(&lf);
    for (const bool bypassed : {false, true}) {
        processor.setBypassed(bypassed);
        auto paintCard = [&](bool picture) {
            CardPanelPicture::setEnabledForTest(picture);
            juce::Image image(juce::Image::ARGB, card.getWidth(), card.getHeight(), true, juce::SoftwareImageType());
            juce::Graphics g(image);
            card.paint(g);
            return image;
        };
        EXPECT_EQ(differingPixels(paintCard(false), paintCard(true)), 0) << "bypassed " << bypassed;
    }
    card.setLookAndFeel(nullptr);
}

TEST(CardPanelPicture, TwoCardsOfTheSameSizeAndStateRenderThePanelOnce) {
    PictureGuard guard;
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(themeAt(DepthLevel::Soft));
    paintPanel(lf, kW, kH, false, false, true);
    paintPanel(lf, kW, kH, false, false, true);
    EXPECT_EQ(CardPanelPicture::renderCountForTest(), 1);
    paintPanel(lf, kW, kH, true, false, true);
    EXPECT_EQ(CardPanelPicture::renderCountForTest(), 2) << "another state is another picture";
}

TEST(CardPanelPicture, AThemeOrDepthChangeRendersThePanelAgain) {
    PictureGuard guard;
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(themeAt(DepthLevel::Soft));
    const auto soft = paintPanel(lf, kW, kH, false, false, true);
    ASSERT_EQ(CardPanelPicture::renderCountForTest(), 1);
    lf.applyTheme(themeAt(DepthLevel::Rich));
    const auto rich = paintPanel(lf, kW, kH, false, false, true);
    EXPECT_EQ(CardPanelPicture::renderCountForTest(), 2);
    EXPECT_GT(differingPixels(soft, rich, 0), 0) << "the new depth shows";
}

TEST(CardPanelPicture, ASizeChangeRendersThePanelAgain) {
    PictureGuard guard;
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(themeAt(DepthLevel::Soft));
    paintPanel(lf, kW, kH, false, false, true);
    paintPanel(lf, kW, kH + 40, false, false, true);
    EXPECT_EQ(CardPanelPicture::renderCountForTest(), 2);
}

TEST(CardPanelPicture, KeepsAtMostSixtyFourPictures) {
    PictureGuard guard;
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(themeAt(DepthLevel::Soft));
    for (int i = 0; i < 70; ++i)
        paintPanel(lf, 100 + i, 60, false, false, true);
    ASSERT_EQ(CardPanelPicture::renderCountForTest(), 70);
    paintPanel(lf, 169, 60, false, false, true); // the newest is still held
    EXPECT_EQ(CardPanelPicture::renderCountForTest(), 70);
    paintPanel(lf, 100, 60, false, false, true); // the oldest was dropped
    EXPECT_EQ(CardPanelPicture::renderCountForTest(), 71);
}
