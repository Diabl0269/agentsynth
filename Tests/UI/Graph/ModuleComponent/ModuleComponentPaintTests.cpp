// ModuleComponent paint/theme tests: Wavetable paint smoke, MIDI keyboard theming, Audio Output card identity.

#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "Modules/MidiKeyboardModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

TEST_F(ModuleComponentTest, WavetableCardPaintsAndTicksWithoutCrashing) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    EXPECT_NO_THROW(moduleComponent.timerCallback());

    juce::Image img(juce::Image::ARGB, moduleComponent.getWidth(), moduleComponent.getHeight(), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(moduleComponent.paint(g));
    EXPECT_TRUE(img.isValid());
}

// Theme switch must recolour the on-screen MidiKeyboardComponent. Colours are set on the
// component itself (not via AppLookAndFeel ColourIds — juce_audio_utils is not linked into
// Core), so lookAndFeelChanged() has to push them again or the keys keep the previous theme.
TEST_F(ModuleComponentTest, MidiKeyboardKeysFollowThemeChange) {
    AudioEngine engine;
    GraphEditor editor(engine);
    MidiKeyboardModule keyboard;
    ModuleComponent moduleComponent(&keyboard, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::MidiKeyboardComponent* keys = nullptr;
    for (int i = 0; i < moduleComponent.getNumChildComponents(); ++i)
        if (auto* kb = dynamic_cast<juce::MidiKeyboardComponent*>(moduleComponent.getChildComponent(i)))
            keys = kb;
    ASSERT_NE(keys, nullptr) << "MIDI Keyboard card must host a MidiKeyboardComponent";

    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    moduleComponent.setLookAndFeel(&lf);
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::whiteNoteColourId),
              synth::theme::makeObsidian().colors.bg1);
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::blackNoteColourId),
              synth::theme::makeObsidian().colors.surfaceHi);
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::keyDownOverlayColourId),
              synth::theme::makeObsidian().colors.accent);

    lf.applyTheme(synth::theme::makeNeon());
    moduleComponent.sendLookAndFeelChange();
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::whiteNoteColourId), synth::theme::makeNeon().colors.bg1);
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::blackNoteColourId),
              synth::theme::makeNeon().colors.surfaceHi);
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::keyDownOverlayColourId),
              synth::theme::makeNeon().colors.accent);
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::keySeparatorLineColourId),
              synth::theme::makeNeon().colors.border);
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::textLabelColourId),
              synth::theme::makeNeon().colors.textPrimary);

    moduleComponent.setLookAndFeel(nullptr);
}

// --- Output-card identity treatment (module chrome) --------------------------
// Audio Output is a bare juce::AudioGraphIOProcessor, not a ModuleBase — setOutputDeviceInfoText
// / getOutputDeviceInfoTextForTest are the seam GraphEditor::refreshOutputDeviceInfo drives
// (MainComponent -> GraphEditor -> here). See docs/layout/module-card.md.

namespace {
/** Adds the graph's terminal audio sink the way AudioEngine does — the channel layout has to be
 *  set BEFORE the node is added (AudioGraphIOProcessor snapshots it once, in setParentGraph). */
juce::AudioProcessorGraph::Node::Ptr addAudioOutputNode(juce::AudioProcessorGraph& graph) {
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    return graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
}
} // namespace

TEST_F(ModuleComponentTest, AudioOutputCardHasNoDeviceInfoTextUntilSet) {
    AudioEngine engine;
    GraphEditor editor(engine);
    auto node = addAudioOutputNode(engine.getGraph());
    ModuleComponent moduleComponent(node->getProcessor(), node->nodeID, editor);

    EXPECT_TRUE(moduleComponent.getOutputDeviceInfoTextForTest().isEmpty());

    // Headless: no themed LookAndFeel, so the CatIO icon is absent — must still not crash.
    juce::Image img(juce::Image::ARGB, moduleComponent.getWidth(), moduleComponent.getHeight(), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(moduleComponent.paint(g));
}

TEST_F(ModuleComponentTest, AudioOutputCardStoresAndPaintsDeviceInfoText) {
    AudioEngine engine;
    GraphEditor editor(engine);
    auto node = addAudioOutputNode(engine.getGraph());
    ModuleComponent moduleComponent(node->getProcessor(), node->nodeID, editor);

    const juce::String deviceText("Test Device - 48 kHz - 2ch");
    moduleComponent.setOutputDeviceInfoText(deviceText);
    EXPECT_EQ(moduleComponent.getOutputDeviceInfoTextForTest(), deviceText);

    // Setting the same text again is not a change (mirrors setModDropTargetChannel's contract) —
    // just confirms it stays idempotent rather than asserting an internal repaint count.
    moduleComponent.setOutputDeviceInfoText(deviceText);
    EXPECT_EQ(moduleComponent.getOutputDeviceInfoTextForTest(), deviceText);

    juce::Image img(juce::Image::ARGB, moduleComponent.getWidth(), moduleComponent.getHeight(), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(moduleComponent.paint(g));

    // Real themed LnF: the identity glyph + muted subtitle path must also survive (see
    // MidiKeyboardKeysFollowThemeChange above for the same real-AppLookAndFeel pattern).
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    moduleComponent.setLookAndFeel(&lf);
    EXPECT_NO_THROW(moduleComponent.paint(g));
    moduleComponent.setLookAndFeel(nullptr);

    // Empty string (e.g. Hosted mode, or the device closing) hides the line again.
    moduleComponent.setOutputDeviceInfoText({});
    EXPECT_TRUE(moduleComponent.getOutputDeviceInfoTextForTest().isEmpty());
}

TEST_F(ModuleComponentTest, SetOutputDeviceInfoTextIsANoOpOnNonOutputModules) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    moduleComponent.setOutputDeviceInfoText("should not apply to a regular module");
    EXPECT_TRUE(moduleComponent.getOutputDeviceInfoTextForTest().isEmpty());
}

// The type-not-name idiom: Audio Input is also a bare AudioGraphIOProcessor, but the WRONG
// IODeviceType, and must not pick up the output-only identity treatment.
TEST_F(ModuleComponentTest, SetOutputDeviceInfoTextIsANoOpOnAudioInputNode) {
    AudioEngine engine;
    GraphEditor editor(engine);
    auto& graph = engine.getGraph();
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    graph.setPlayConfigDetails(2, 0, 44100.0, 512);
    auto node = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioInputNode));
    ModuleComponent moduleComponent(node->getProcessor(), node->nodeID, editor);

    moduleComponent.setOutputDeviceInfoText("should not apply to Audio Input");
    EXPECT_TRUE(moduleComponent.getOutputDeviceInfoTextForTest().isEmpty());
}

// --- Output-card identity glyph alignment/sizing (visual follow-up) ----------
// outputCardIconBoundsForTest() is the exact geometry ModuleComponent::paint() draws the CatIO
// glyph into — see docs/layout/module-card.md's Audio Output card identity. These tests recompute
// the same public JUCE font-metric calls independently (never reach into ModuleComponent's private
// paint code) so they pin the FORMULA/contract, not a platform-specific pixel constant.

TEST(ModuleComponentOutputIconBounds, IconIsSquareAndProportionalToTitleCapHeightNotTheFullHeaderBand) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());

    const auto bounds = ModuleComponent::outputCardIconBoundsForTest(lf);

    const juce::Font titleFont(juce::FontOptions(lf.getTheme().type.h2, juce::Font::bold));
    const float expectedCapHeight = titleFont.getAscent() * 0.72f;

    EXPECT_FLOAT_EQ(bounds.getWidth(), bounds.getHeight()) << "the glyph must stay square";
    EXPECT_NEAR(bounds.getHeight(), expectedCapHeight, 0.01f);
    // Proportional to the title, not the header band: strictly smaller than both the previous
    // fixed 16px box and the header's own 24px height.
    EXPECT_LT(bounds.getHeight(), 16.0f);
    EXPECT_LT(bounds.getHeight(), 24.0f);
    EXPECT_LT(bounds.getHeight(), titleFont.getHeight())
        << "must not be sized off the full ascent+descent box the title's own text is drawn in";
}

TEST(ModuleComponentOutputIconBounds, IconRightEdgeMatchesTheActivityLEDsRightEdgeForAnEightPxTextGap) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeNeon()); // a different theme: the geometry must not be theme-dependent

    const auto bounds = ModuleComponent::outputCardIconBoundsForTest(lf);

    // The activity LED is fillEllipse(6, 8, 8, 8) in ModuleComponent::paint() -> right edge x=14.
    // The title's own left inset is 22 (AppLookAndFeel::drawModulePanel). Pinning the icon's right
    // edge to the LED's is what keeps that established 8px gap regardless of the icon's width.
    EXPECT_NEAR(bounds.getRight(), 14.0f, 0.001f);
    constexpr float kTitleLeftInset = 22.0f;
    EXPECT_NEAR(kTitleLeftInset - bounds.getRight(), 8.0f, 0.001f);
}

TEST(ModuleComponentOutputIconBounds, IconIsVerticallyCentredOnTheTitlesCapHeightNotTheHeaderBandsMidline) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());

    const auto bounds = ModuleComponent::outputCardIconBoundsForTest(lf);

    const juce::Font titleFont(juce::FontOptions(lf.getTheme().type.h2, juce::Font::bold));
    const float capHeight = titleFont.getAscent() * 0.72f;
    constexpr float kHeaderTop = 2.0f;
    constexpr float kHeaderHeight = 24.0f;
    const float textBoxTop = kHeaderTop + (kHeaderHeight - titleFont.getHeight()) * 0.5f;
    const float baseline = textBoxTop + titleFont.getAscent();
    const float expectedCapCentreY = baseline - capHeight * 0.5f;

    EXPECT_NEAR(bounds.getCentreY(), expectedCapCentreY, 0.01f);
    // Sanity: still fully inside the 24px header band ([2, 26] in local coordinates), whatever the
    // exact resolved font metrics turn out to be on this platform.
    EXPECT_GE(bounds.getY(), kHeaderTop);
    EXPECT_LE(bounds.getBottom(), kHeaderTop + kHeaderHeight);
}

// Colour lockup: the glyph must follow the title's own colour token, not the library sidebar's
// fixed textMuted bake — rendered end-to-end through paint() (not the bounds helper), because the
// tint swap happens at paint time on a per-call clone.
TEST_F(ModuleComponentTest, AudioOutputCardIconTintsToTheTitleColourNotTheLibraryMutedTint) {
    AudioEngine engine;
    GraphEditor editor(engine);
    auto node = addAudioOutputNode(engine.getGraph());
    ModuleComponent moduleComponent(node->getProcessor(), node->nodeID, editor);

    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    moduleComponent.setLookAndFeel(&lf);

    juce::Image img(juce::Image::ARGB, moduleComponent.getWidth(), moduleComponent.getHeight(), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(moduleComponent.paint(g));

    const auto bounds = ModuleComponent::outputCardIconBoundsForTest(lf).getSmallestIntegerContainer().expanded(1);
    // titleColour (not selected, not bypassed) is textPrimary — near-white — checked inline below.
    const auto libraryMutedTint = synth::theme::makeObsidian().colors.textMuted; // the OLD (wrong) tint

    // Counts, not a single any-pixel boolean: the icon is tiny (~cap-height px) against a dark
    // header, so its anti-aliased edges sweep through every grey between white and the background
    // on the way down — a LOOSE proximity check against an arbitrary mid-grey reference will always
    // find some blended edge pixel near it, tint bug or not. A near-EXACT match (tight tolerance)
    // over a MEANINGFUL fraction of the sampled pixels is what actually distinguishes "still solid-
    // filled with the old textMuted tint" from ordinary antialiasing.
    int nearWhiteCount = 0;
    int exactMutedCount = 0;
    int opaqueSamples = 0;
    for (int y = bounds.getY(); y < bounds.getBottom(); ++y) {
        for (int x = bounds.getX(); x < bounds.getRight(); ++x) {
            if (!img.getBounds().contains(x, y))
                continue;
            const auto p = img.getPixelAt(x, y);
            if (p.getAlpha() < 200)
                continue; // skip transparent pixels (the header is opaque, so this rarely fires)
            ++opaqueSamples;
            if (p.getRed() > 200 && p.getGreen() > 200 && p.getBlue() > 200)
                ++nearWhiteCount; // textPrimary (0xffEAEEF3) is near-white
            if (std::abs((int)p.getRed() - (int)libraryMutedTint.getRed()) <= 3 &&
                std::abs((int)p.getGreen() - (int)libraryMutedTint.getGreen()) <= 3 &&
                std::abs((int)p.getBlue() - (int)libraryMutedTint.getBlue()) <= 3)
                ++exactMutedCount;
        }
    }
    ASSERT_GT(opaqueSamples, 0);
    EXPECT_GT(nearWhiteCount, 0) << "the glyph should render in the title's (near-white) colour";
    // A regression that dropped the replaceColour() call would leave the WHOLE glyph solid-filled
    // in textMuted, not one stray edge pixel — so even a generous few-percent allowance still fails
    // that case hard while tolerating antialiasing.
    EXPECT_LE(exactMutedCount, opaqueSamples / 10)
        << "the glyph must not still be (mostly) solid-filled in the library sidebar's muted grey";

    moduleComponent.setLookAndFeel(nullptr);
}
