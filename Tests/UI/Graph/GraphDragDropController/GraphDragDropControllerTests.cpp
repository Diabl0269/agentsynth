// GraphDragDropControllerTests.cpp
//
// Controller-level coverage for GraphDragDropController (FRO77 PR3): drives it directly against a
// real AudioEngine graph through GraphEditor::getCanvasHostForTest() (a minimal host — GraphEditor
// supplies real ModuleComponents/graph/undo, but the test never goes through GraphEditor's own
// itemDragEnter/itemDragMove/itemDragExit/itemDropped forwarders — it builds a SEPARATE
// GraphDragDropController instance and drives DragAndDropTarget::SourceDetails through it
// directly, proving the controller's own API works in isolation. The existing
// GraphEditorSmartConnection*Tests.cpp / GraphEditorLayoutTests.cpp files cover the full gesture
// chain through GraphEditor's forwarders and are unchanged by this PR — this file mirrors
// SmartConnectionEngineTests.cpp's own isolation approach (FRO77 PR1).

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AudioEngine/AudioEngine.h"

#include "UI/Graph/GraphDragDropController/GraphDragDropController.h"

TEST(GraphDragDropControllerTest, EnterMoveExitClearsPreview) {
    AudioEngine audioEngine;
    GraphEditor editor(audioEngine);
    editor.setSize(800, 600);

    GraphDragDropController controllerUnderTest(editor.getCanvasHostForTest());
    EXPECT_FALSE(controllerUnderTest.isDragPreviewActive());

    DummyDragSource source;
    juce::DragAndDropTarget::SourceDetails details("Oscillator", &source, juce::Point<int>(100, 100));

    EXPECT_TRUE(controllerUnderTest.isInterestedInDragSource(details));

    controllerUnderTest.itemDragEnter(details);
    EXPECT_TRUE(controllerUnderTest.isDragPreviewActive());
    EXPECT_FALSE(controllerUnderTest.getDragPreviewGhost().isEmpty());

    controllerUnderTest.itemDragMove(details);
    EXPECT_TRUE(controllerUnderTest.isDragPreviewActive()) << "a move tick must not itself clear the preview";

    controllerUnderTest.itemDragExit(details);
    EXPECT_FALSE(controllerUnderTest.isDragPreviewActive());
    EXPECT_TRUE(controllerUnderTest.getDragPreviewGhost().isEmpty());
}

TEST(GraphDragDropControllerTest, DropOfAModuleDescriptionAddsOneNodeAtTheSnappedPosition) {
    AudioEngine audioEngine;
    GraphEditor editor(audioEngine);
    editor.setSize(800, 600);

    GraphDragDropController controllerUnderTest(editor.getCanvasHostForTest());
    auto& graph = audioEngine.getGraph();
    ASSERT_EQ(graph.getNumNodes(), 0);

    DummyDragSource source;
    // No preceding itemDragEnter/itemDragMove: itemDropped falls back to the centred cursor
    // position (see the class doc's "no live ghost" branch, exercised by tests deliberately).
    juce::DragAndDropTarget::SourceDetails details("Oscillator", &source, juce::Point<int>(100, 100));

    controllerUnderTest.itemDropped(details);

    ASSERT_EQ(graph.getNumNodes(), 1);
    auto node = graph.getNodes().getFirst();
    ASSERT_NE(node, nullptr);
    ASSERT_NE(node->getProcessor(), nullptr);
    EXPECT_EQ(node->getProcessor()->getName(), "Oscillator");

    // The drop position is snapped to the layout grid, exactly like any other placement — not the
    // raw, unsnapped cursor point.
    const int x = (int)node->properties.getWithDefault("x", -1);
    const int y = (int)node->properties.getWithDefault("y", -1);
    EXPECT_EQ(x % synth::LayoutUtil::kGridSize, 0) << "x=" << x << " must land on the layout grid";
    EXPECT_EQ(y % synth::LayoutUtil::kGridSize, 0) << "y=" << y << " must land on the layout grid";

    EXPECT_FALSE(controllerUnderTest.isDragPreviewActive()) << "itemDropped must tear down the preview it started";
}
