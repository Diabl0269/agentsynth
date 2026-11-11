// TimelinePanelMakeSectionKeyTests.cpp -- the Make Section key (Shift+Cmd+J) works from the whole timeline, not only a
// lane with a range: from the Sections strip and the lanes it makes a section from the Range tool's bars, else the
// loop, else a toast says what to do; the strip's tooltip names the key only when it would make something.

#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/TransportService.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

struct KeyPanel {
    synth::TimelineDoc doc;
    synth::TransportService transport;
    AppUndoManager undo;
    synth::ui::TimelinePanelComponent panel;
    synth::TrackId track;
    juce::StringArray toasts;

    KeyPanel() {
        transport.prepare(48000.0, 512);
        track = doc.addTrack(synth::TrackKind::Midi, "Lead");
        doc.addClip(track, 0.0, 4.0, "A");
        panel.setTimelineDoc(&doc);
        panel.setTransport(&transport);
        panel.setUndoManager(&undo);
        panel.setSize(1200, 600);
        panel.onShowMessage = [this](const juce::String& text) { toasts.add(text); };
    }
    void setLoop(double start, double end) {
        ASSERT_TRUE(transport.setLoop(start, end, true));
        transport.tick(512);
    }
    // The strip does not take the key itself, so JUCE hands it to the panel, its parent.
    bool pressOnStrip() {
        const auto key = synth::ui::TimelineClipLaneArea::sectionKey();
        return panel.getSectionStrip().keyPressed(key) || panel.keyPressed(key);
    }
    juce::String emptyStripTooltip() { return panel.getSectionStrip().tooltipAt({1100.0f, 10.0f}); }
};

} // namespace

TEST(TimelinePanelMakeSectionKeyTest, FromTheStripTheKeyMakesASectionFromTheLoopAsOneUndoStep) {
    KeyPanel f;
    f.setLoop(8.0, 16.0);
    EXPECT_TRUE(f.emptyStripTooltip().contains("to make one from the loop")) << f.emptyStripTooltip();
    EXPECT_TRUE(f.pressOnStrip());
    ASSERT_EQ(f.doc.getPlacements().size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[0].startBeat, 8.0);
    EXPECT_DOUBLE_EQ(f.doc.getSection(f.doc.getPlacements()[0].section)->lengthBeats, 8.0);
    EXPECT_TRUE(f.toasts.isEmpty());
    EXPECT_TRUE(f.undo.undo());
    EXPECT_TRUE(f.doc.getPlacements().empty());
}

TEST(TimelinePanelMakeSectionKeyTest, TheRangeToolsBarsWinOverTheLoop) {
    KeyPanel f;
    f.setLoop(16.0, 24.0);
    f.panel.getClipLaneArea().setRange(f.track, 0.0, f.track, 8.0);
    EXPECT_TRUE(f.emptyStripTooltip().contains("to make one from the selected bars")) << f.emptyStripTooltip();
    EXPECT_TRUE(f.pressOnStrip());
    ASSERT_EQ(f.doc.getPlacements().size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[0].startBeat, 0.0);
}

TEST(TimelinePanelMakeSectionKeyTest, WithNothingToMakeItFromTheKeySaysWhatToDoAndTheTooltipPromisesNothing) {
    KeyPanel f;
    EXPECT_FALSE(f.emptyStripTooltip().contains("press")) << f.emptyStripTooltip();
    EXPECT_TRUE(f.pressOnStrip()) << "the key is answered, never silently dropped";
    EXPECT_TRUE(f.doc.getPlacements().empty());
    ASSERT_EQ(f.toasts.size(), 1);
    EXPECT_TRUE(f.toasts[0].contains("Range tool")) << f.toasts[0];
}

TEST(TimelinePanelMakeSectionKeyTest, ALoopOverAPlacedSectionIsRefusedWithAReason) {
    KeyPanel f;
    f.doc.makeSection(0.0, 8.0, "Intro", 0xff56D4C0, 0.5);
    f.panel.getSectionStrip().refresh();
    f.setLoop(4.0, 12.0);
    EXPECT_FALSE(f.emptyStripTooltip().contains("press")) << f.emptyStripTooltip();
    EXPECT_TRUE(f.pressOnStrip());
    EXPECT_EQ(f.doc.getPlacements().size(), 1u);
    ASSERT_EQ(f.toasts.size(), 1);
    EXPECT_TRUE(f.toasts[0].contains("already hold a section")) << f.toasts[0];
}
