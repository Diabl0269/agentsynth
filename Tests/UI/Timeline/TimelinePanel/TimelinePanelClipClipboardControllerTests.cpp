// Clip CC lanes are part of the clip: copy/paste, cut/paste, duplicate and repeat all carry
// Clip::controllers (docs/timeline/piano-roll-lanes.md#data-model).

#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>

namespace {

struct CcClipboardFixture {
    synth::TimelineDoc doc;
    AppUndoManager undo;
    synth::ui::TimelinePanelComponent panel;
    synth::TrackId track;
    synth::ClipId clip;

    CcClipboardFixture() {
        panel.setSize(1200, 320);
        panel.getViewState().snap = synth::ui::TimelineViewState::Snap::Quarter;
        panel.getViewState().snapEnabled = true;
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        track = doc.addTrack(synth::TrackKind::Midi, "T");
        clip = doc.addClip(track, 4.0, 4.0, "c");
        doc.setControllerLanePoints(clip, 1, {{0.0, 10.0, 1}, {2.0, 90.0, 1}});
        doc.setControllerLanePoints(clip, 64, {{1.0, 127.0, 0}});
        panel.getClipSelection().setSelection({clip});
    }

    // The CC lanes of every clip on the track except the source, in clip order.
    std::vector<const synth::Clip*> copies() const {
        std::vector<const synth::Clip*> out;
        for (const auto& c : doc.getTrack(track)->clips)
            if (c.id != clip)
                out.push_back(&c);
        return out;
    }

    static void expectSameLanes(const synth::Clip& copy, const synth::Clip& source) {
        ASSERT_EQ(copy.controllers.size(), source.controllers.size());
        for (size_t i = 0; i < copy.controllers.size(); ++i) {
            EXPECT_EQ(copy.controllers[i].ccNumber, source.controllers[i].ccNumber);
            ASSERT_EQ(copy.controllers[i].points.size(), source.controllers[i].points.size());
            for (size_t p = 0; p < copy.controllers[i].points.size(); ++p) {
                EXPECT_DOUBLE_EQ(copy.controllers[i].points[p].beat, source.controllers[i].points[p].beat);
                EXPECT_DOUBLE_EQ(copy.controllers[i].points[p].value, source.controllers[i].points[p].value);
                EXPECT_EQ(copy.controllers[i].points[p].curve, source.controllers[i].points[p].curve);
            }
        }
    }
};

} // namespace

TEST(TimelineClipClipboardControllerTest, CopyPasteCarriesCcLanes) {
    CcClipboardFixture f;
    ASSERT_TRUE(f.panel.copySelectedClips());
    ASSERT_TRUE(f.panel.pasteClipsAtPlayhead());
    const auto copies = f.copies();
    ASSERT_EQ(copies.size(), 1u);
    CcClipboardFixture::expectSameLanes(*copies[0], *f.doc.getClip(f.clip));
}

TEST(TimelineClipClipboardControllerTest, CutPasteCarriesCcLanes) {
    CcClipboardFixture f;
    const synth::Clip source = *f.doc.getClip(f.clip);
    ASSERT_TRUE(f.panel.cutSelectedClips());
    ASSERT_TRUE(f.panel.pasteClipsAtPlayhead());
    const auto& clips = f.doc.getTrack(f.track)->clips;
    ASSERT_EQ(clips.size(), 1u);
    CcClipboardFixture::expectSameLanes(clips[0], source);
}

TEST(TimelineClipClipboardControllerTest, DuplicateAndRepeatCarryCcLanes) {
    CcClipboardFixture f;
    ASSERT_TRUE(f.panel.duplicateSelectedClips());
    f.panel.getClipSelection().setSelection({f.clip});
    ASSERT_TRUE(f.panel.repeatSelectedClips(2));
    const auto copies = f.copies();
    ASSERT_EQ(copies.size(), 3u);
    for (const auto* copy : copies)
        CcClipboardFixture::expectSameLanes(*copy, *f.doc.getClip(f.clip));
}
