// A Sound dragged from the Sounds view onto the canvas: sized as a Sampler, never read as a module type, and dropped as
// the same one-undo-step Sampler an OS file drop makes.
#include "../../../Modules/Sampler/SamplerTestWavs.h"
#include "../CardBody/CardBodyTestHelpers.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "Modules/SamplerModule.h"
#include "UI/Graph/GraphDragDropController/GraphDragDropController.h"
#include "UI/Library/SoundBrowser/SoundDragPayload.h"

using namespace cardbody_test;
using namespace sampler_test;

namespace {

SamplerModule* onlySampler(AudioEngine& engine) {
    auto nodes = engine.getGraph().getNodes();
    return nodes.size() == 1 ? dynamic_cast<SamplerModule*>(nodes.getFirst()->getProcessor()) : nullptr;
}

} // namespace

TEST(GraphDragDropControllerSoundTest, TheGhostIsASamplerAndTheDropIsOneUndoableSamplerHoldingTheSound) {
    TempDir dir;
    const auto wav = dir.file("kick.wav");
    ASSERT_TRUE(writeWav(wav, 4410, 1, 44100.0, [](int, int) { return 0.3f; }));

    CardCanvas canvas;
    GraphDragDropController controller(canvas.editor.getCanvasHostForTest());
    DummyDragSource source;
    juce::DragAndDropTarget::SourceDetails details(synth::ui::makeSoundPayload(wav.getFullPathName()), &source,
                                                   {300, 300});

    EXPECT_TRUE(controller.isInterestedInDragSource(details));
    controller.itemDragEnter(details);
    ASSERT_TRUE(controller.isDragPreviewActive());
    const auto size = GraphEditor::estimateModuleSize("Sampler");
    EXPECT_EQ(controller.getDragPreviewGhost().getWidth(), size.x);
    EXPECT_EQ(controller.getDragPreviewGhost().getHeight(), size.y);
    controller.itemDragMove(details);

    ASSERT_EQ(canvas.engine.getGraph().getNumNodes(), 0);
    controller.itemDropped(details);

    ASSERT_EQ(canvas.engine.getGraph().getNumNodes(), 1) << "the payload never became a module type name";
    auto* sampler = onlySampler(canvas.engine);
    ASSERT_NE(sampler, nullptr);
    EXPECT_EQ(sampler->getSampleFilePath(), wav.getFullPathName());
    EXPECT_FALSE(controller.isDragPreviewActive());

    ASSERT_TRUE(canvas.undo.undo());
    EXPECT_EQ(canvas.engine.getGraph().getNumNodes(), 0) << "the Sampler and its sample are ONE undo step";
    EXPECT_FALSE(canvas.undo.undo());
}

TEST(GraphDragDropControllerSoundTest, AnSfzDropsAsAnInstrumentAndAFileThatIsNeitherIsRefused) {
    TempDir dir;
    ASSERT_TRUE(writeWav(dir.file("a.wav"), 4410, 1, 44100.0, [](int, int) { return 0.3f; }));
    const auto sfz = dir.file("keys.sfz");
    sfz.replaceWithText("<region> sample=a.wav key=60\n");

    CardCanvas canvas;
    GraphDragDropController controller(canvas.editor.getCanvasHostForTest());
    DummyDragSource source;
    juce::DragAndDropTarget::SourceDetails notes(synth::ui::makeSoundPayload(dir.file("notes.txt").getFullPathName()),
                                                 &source, {300, 300});
    EXPECT_FALSE(controller.isInterestedInDragSource(notes));

    juce::DragAndDropTarget::SourceDetails details(synth::ui::makeSoundPayload(sfz.getFullPathName()), &source,
                                                   {300, 300});
    EXPECT_TRUE(controller.isInterestedInDragSource(details));
    controller.itemDropped(details);
    auto* sampler = onlySampler(canvas.engine);
    ASSERT_NE(sampler, nullptr);
    EXPECT_GT(sampler->getInstrumentGeneration(), 0) << "the .sfz loaded as the Sampler's instrument";
}

TEST(GraphDragDropControllerSoundTest, TheSharedPathIsWhatAnOsFileDropUses) {
    TempDir dir;
    const auto wav = dir.file("snare.wav");
    ASSERT_TRUE(writeWav(wav, 4410, 1, 44100.0, [](int, int) { return 0.3f; }));
    CardCanvas canvas;
    GraphDragDropController controller(canvas.editor.getCanvasHostForTest());
    controller.filesDropped({wav.getFullPathName()}, 200, 200);
    auto* sampler = onlySampler(canvas.engine);
    ASSERT_NE(sampler, nullptr);
    EXPECT_EQ(sampler->getSampleFilePath(), wav.getFullPathName());
}
