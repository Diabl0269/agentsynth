// MixerFaderTests.cpp -- FRO11 (P9-5): a fader gesture changes gain as exactly one undo step.
//
// Deviation from the plan's "drive it through real synthesised mouse events on the juce::Slider"
// (docs/testing.md's own convention, MacroPortRealMouseDragTests.cpp's template): a raw
// mouseDown/mouseDrag/mouseUp sequence built by hand and fed straight to a stock juce::Slider (as
// opposed to this codebase's own Component subclasses, which is what that template actually
// drives) hung this suite in CI -- juce::Slider's own internal mouse handling reaches into
// platform mouse-capture/cursor code the other real-mouse tests here never touch, since they all
// drive purpose-built components, never a stock library Slider, this way. Driving the bound
// AudioParameterFloat's own beginChangeGesture()/setValueNotifyingHost()/endChangeGesture() -- the
// exact three calls juce::SliderParameterAttachment's internal Slider::Listener makes in response
// to a real drag -- exercises MixerFader::parameterGestureChanged (the undo bracket under test)
// through the identical listener callback a mouse drag would trigger, without going through
// Slider's own native mouse path. Follow-up: revisit a real mouse-driven version if this hang gets
// root-caused.
//
// This test also caught a real bug (fixed alongside it, not just in this test): MixerFader's own
// parameterValueChanged() queued a MessageManager::callAsync capturing a raw `this` instead of a
// juce::Component::SafePointer -- unbind()/destruction doesn't cancel an already-queued callback,
// so a fader torn down between the gesture and the queued message running (here: at end of scope;
// in the app: a graph rebuild from undo/redo or MixerPanelComponent::rebuild()) dereferenced freed
// memory. See MixerFader::parameterValueChanged's own comment.
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Mixer/MixerFader.h"
#include "UI/Mixer/MixerFaderTaper.h"
#include <gtest/gtest.h>

TEST(MixerFaderTests, FaderGestureChangesGainAsOneUndoStep) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    AppUndoManager undoManager;

    auto processor = std::make_unique<ChannelStripModule>();
    processor->setShape(ChannelStripModule::Shape::Stereo);
    auto node = graph.addNode(std::move(processor));
    ASSERT_NE(node, nullptr);
    juce::AudioParameterFloat* gainParam = nullptr;
    for (auto* param : node->getProcessor()->getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(param); f != nullptr && f->paramID == "gain")
            gainParam = f;
    ASSERT_NE(gainParam, nullptr);

    synth::ui::MixerFader fader;
    fader.setSize(24, 200);
    fader.bind(graph, undoManager, *gainParam);

    const float before = gainParam->get();

    // The exact sequence SliderParameterAttachment's own Slider::Listener makes around a real
    // drag -- see this file's header comment for why this drives the param directly rather than
    // juce::Slider's own mouse handling.
    gainParam->beginChangeGesture();
    gainParam->setValueNotifyingHost(gainParam->convertTo0to1(gainParam->get()) < 0.5f ? 0.95f : 0.05f);
    gainParam->endChangeGesture();

    const float after = gainParam->get();
    EXPECT_NE(after, before) << "the gesture must have moved the gain parameter";
    ASSERT_TRUE(undoManager.canUndo());
    ASSERT_TRUE(undoManager.undo());
    // Near, not bit-exact: the undo snapshot restores through the param's normalised [0,1] range
    // (AudioProcessorParameter's own convertTo0to1/convertFrom0to1), which is a float round-trip
    // and not guaranteed bit-identical for every value -- 1e-4 dB is far below anything audible or
    // visible on the fader.
    EXPECT_NEAR(gainParam->get(), before, 1.0e-4f) << "one undo must restore the pre-drag gain";
    EXPECT_FALSE(undoManager.canUndo()) << "the whole gesture must have collapsed to ONE undo step";

    fader.unbind();
}

// FRO150: proves the bound slider's on-screen THUMB POSITION follows MixerFaderTaper.h while the
// bound parameter's own value stays exactly linear dB -- the two must never drift, since the
// taper is UI-only (MixerFader.h's own class comment) and the parameter is what presets,
// automation, AI patches and the plugin host all read.
TEST(MixerFaderTests, BoundSliderPositionMatchesTaperWhileParameterStaysLinearDb) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    AppUndoManager undoManager;

    auto processor = std::make_unique<ChannelStripModule>();
    processor->setShape(ChannelStripModule::Shape::Stereo);
    auto node = graph.addNode(std::move(processor));
    ASSERT_NE(node, nullptr);
    juce::AudioParameterFloat* gainParam = nullptr;
    for (auto* param : node->getProcessor()->getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(param); f != nullptr && f->paramID == "gain")
            gainParam = f;
    ASSERT_NE(gainParam, nullptr);

    synth::ui::MixerFader fader;
    fader.setSize(24, 200);
    fader.bind(graph, undoManager, *gainParam);

    for (const float db : {-14.2f, 0.0f, 12.0f}) {
        gainParam->beginChangeGesture();
        gainParam->setValueNotifyingHost(gainParam->convertTo0to1(db));
        gainParam->endChangeGesture();

        // The parameter itself: exactly the dB set (0.1 dB param interval, so near not bit-exact).
        EXPECT_NEAR(gainParam->get(), db, 1.0e-3f) << "db=" << db;

        // The slider's own value mirrors the param (unchanged by the taper).
        EXPECT_NEAR(fader.getSlider().getValue(), (double)db, 1.0e-2) << "db=" << db;

        // The slider's THUMB POSITION -- getNormalisableRange().convertTo0to1(value) -- is the
        // taper's fraction for that dB, not a linear (db - min) / (max - min) fraction.
        const double position = fader.getSlider().getNormalisableRange().convertTo0to1(fader.getSlider().getValue());
        EXPECT_NEAR(position, (double)synth::ui::faderDbToFraction(db), 1.0e-3) << "db=" << db;
    }

    fader.unbind();
}

// FRO150 regression: juce::SliderParameterAttachment's own constructor overwrites
// textFromValueFunction with one built from the param's own getText() (no " dB" suffix --
// ChannelStripModule's gain param has no unit label) -- caught while reordering bind() so the
// taper range survives attachment_'s construction (MixerFader::bind()'s own comment). Proves the
// dB-formatted accessibility text (MixerAccessibilityTest.FaderTextFromValueFunctionFormatsAsMinusThreeDb,
// which only ever checks an UNBOUND fader) still holds after a REAL bind().
TEST(MixerFaderTests, TextFromValueFunctionStillReportsDbAfterBind) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    AppUndoManager undoManager;

    auto processor = std::make_unique<ChannelStripModule>();
    processor->setShape(ChannelStripModule::Shape::Stereo);
    auto node = graph.addNode(std::move(processor));
    juce::AudioParameterFloat* gainParam = nullptr;
    for (auto* param : node->getProcessor()->getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(param); f != nullptr && f->paramID == "gain")
            gainParam = f;
    ASSERT_NE(gainParam, nullptr);

    synth::ui::MixerFader fader;
    fader.bind(graph, undoManager, *gainParam);

    ASSERT_TRUE((bool)fader.getSlider().textFromValueFunction);
    EXPECT_EQ(fader.getSlider().textFromValueFunction(-3.0), "-3.0 dB");
    EXPECT_EQ(fader.getSlider().textFromValueFunction(0.0), "0.0 dB");

    fader.unbind();
}
