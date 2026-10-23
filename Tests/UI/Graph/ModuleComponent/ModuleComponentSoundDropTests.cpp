// A recording dropped on a Sampler card, from the OS or dragged out of the Sounds view: one undoable replace, the
// faint outline while a sound is dragged, and the waveform's replace motion.
#include "../../../Modules/Sampler/SamplerTestWavs.h"
#include "../CardBody/CardBodyTestHelpers.h"
#include "Modules/OscillatorModule.h"
#include "Modules/SamplerModule.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerChrome.h"
#include "UI/Library/SoundBrowser/SoundDragPayload.h"
#include "UI/Library/SoundBrowser/SoundDragState.h"

using namespace cardbody_test;
using namespace sampler_test;
using synth::ui::SoundDragState;

namespace {

struct SamplerScene {
    TempDir dir;
    CardCanvas canvas;
    NodeID id;
    juce::File first = dir.file("first.wav");
    juce::File second = dir.file("second.wav");

    SamplerScene() {
        writeWav(first, 4410, 1, 44100.0, [](int, int) { return 0.2f; });
        writeWav(second, 8820, 1, 44100.0, [](int, int) { return 0.4f; });
        id = canvas.add(std::make_unique<SamplerModule>(), 0, 0);
        sampler().loadSampleFile(first);
        canvas.editor.updateComponents();
    }
    SamplerModule& sampler() { return *dynamic_cast<SamplerModule*>(canvas.processor(id)); }
    ModuleComponent& card() { return *canvas.card(id); }
    // The graph's only Sampler now: an undo may rebuild the node, so it is looked up again.
    juce::String loadedPath() {
        for (auto* node : canvas.engine.getGraph().getNodes())
            if (auto* s = dynamic_cast<SamplerModule*>(node->getProcessor()))
                return s->getSampleFilePath();
        return {};
    }
};

} // namespace

TEST(ModuleComponentSoundDropTest, AnOsFileDropReplacesTheSampleInOneUndoStep) {
    SamplerScene s;
    ASSERT_TRUE(s.card().isInterestedInFileDrag({s.second.getFullPathName()}));
    s.card().filesDropped({s.second.getFullPathName()}, 10, 10);
    EXPECT_EQ(s.loadedPath(), s.second.getFullPathName());
    ASSERT_TRUE(s.canvas.undo.undo());
    EXPECT_EQ(s.loadedPath(), s.first.getFullPathName()) << "one Cmd+Z puts the previous sample back";
    ASSERT_TRUE(s.canvas.undo.redo());
    EXPECT_EQ(s.loadedPath(), s.second.getFullPathName());
}

TEST(ModuleComponentSoundDropTest, ADraggedSoundReplacesTheSampleTheSameWay) {
    SamplerScene s;
    juce::DragAndDropTarget::SourceDetails details(synth::ui::makeSoundPayload(s.second.getFullPathName()), nullptr,
                                                   {10, 10});
    ASSERT_TRUE(s.card().isInterestedInDragSource(details));
    s.card().itemDragEnter(details);
    EXPECT_TRUE(s.card().isFileDragHighlighted()) << "the full highlight is the file drop's own";
    s.card().itemDropped(details);
    EXPECT_FALSE(s.card().isFileDragHighlighted());
    EXPECT_EQ(s.loadedPath(), s.second.getFullPathName());
    ASSERT_TRUE(s.canvas.undo.undo());
    EXPECT_EQ(s.loadedPath(), s.first.getFullPathName());
    EXPECT_FALSE(s.canvas.undo.undo()) << "exactly one step";
}

TEST(ModuleComponentSoundDropTest, TheSampleNameFollowsAnUndoAndARedo) {
    SamplerScene s;
    s.card().filesDropped({s.second.getFullPathName()}, 10, 10);
    const auto label = [&s] {
        auto* chrome = dynamic_cast<synth::ui::SamplerChrome*>(s.canvas.card(s.id)->getInstrumentChrome());
        chrome->poll();
        return chrome->getSampleNameLabel().getText();
    };
    EXPECT_EQ(label(), "second.wav");
    ASSERT_TRUE(s.canvas.undo.undo());
    EXPECT_EQ(label(), "first.wav") << "the card names the sample the undo put back";
    ASSERT_TRUE(s.canvas.undo.redo());
    EXPECT_EQ(label(), "second.wav");
}

TEST(ModuleComponentSoundDropTest, OnlyACardThatTakesTheFileTakesTheSound) {
    SamplerScene s;
    const auto osc = s.canvas.add(std::make_unique<OscillatorModule>(), 600, 0);
    s.canvas.editor.updateComponents();
    juce::DragAndDropTarget::SourceDetails wav(synth::ui::makeSoundPayload(s.second.getFullPathName()), nullptr,
                                               {1, 1});
    EXPECT_FALSE(s.canvas.card(osc)->isInterestedInDragSource(wav));
    juce::DragAndDropTarget::SourceDetails module("Oscillator", nullptr, {1, 1});
    EXPECT_FALSE(s.card().isInterestedInDragSource(module)) << "a library drag must reach the canvas";
}

TEST(ModuleComponentSoundDropTest, EveryCardThatWouldTakeTheSoundShowsAFaintOutlineWhileItIsDragged) {
    SamplerScene s;
    const auto osc = s.canvas.add(std::make_unique<OscillatorModule>(), 600, 0);
    s.canvas.editor.updateComponents();
    EXPECT_FALSE(s.card().isShowingSoundOutline());
    SoundDragState::begin(s.second);
    EXPECT_TRUE(s.card().isShowingSoundOutline());
    EXPECT_FALSE(s.canvas.card(osc)->isShowingSoundOutline());
    SoundDragState::end();
    EXPECT_FALSE(s.card().isShowingSoundOutline()) << "the outline clears when the drag ends";
}

TEST(ModuleComponentSoundDropTest, TheWaveformDrawsTheNewSampleInLeftToRightOverTheOldOne) {
    synth::ui::setReducedMotionForTest(false);
    SamplerScene s;
    auto& waveform = dynamic_cast<synth::ui::SamplerChrome*>(s.card().getInstrumentChrome())->getWaveform();
    waveform.setAnimateOffscreenForTest(true);
    EXPECT_FLOAT_EQ(waveform.getReveal(), 1.0f);
    s.card().filesDropped({s.second.getFullPathName()}, 10, 10);
    EXPECT_TRUE(waveform.isRevealRunning());
    EXPECT_FLOAT_EQ(waveform.getReveal(), 0.0f);
    waveform.stepRevealForTest(110.0);
    EXPECT_GT(waveform.getReveal(), 0.0f);
    EXPECT_LT(waveform.getReveal(), 1.0f);
    waveform.finishRevealForTest();
    EXPECT_FLOAT_EQ(waveform.getReveal(), 1.0f);
    synth::ui::setReducedMotionForTest(std::nullopt);
}
