// TimelineClipLaneTestFixture.h — shared fixture/helpers for the TimelineClipLane test split.
#pragma once

#include "AppUndoManager.h"
#include "Modules/RecordTapModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/TransportService.h"
#include "UI/Timeline/ClipSelectionModel.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include "UI/Timeline/TimelineViewState.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <tuple>
#include <vector>

using synth::ClipId;
using synth::TimelineDoc;
using synth::TrackKind;
using synth::ui::ClipSelectionModel;
using synth::ui::TimelineClipLaneArea;
using synth::ui::TimelineViewState;

namespace {
ClipId cid(std::int64_t v) { return ClipId{v}; }

synth::MidiNote makeNote(double startBeat, int pitch, double lengthBeats = 1.0) {
    synth::MidiNote note;
    note.startBeat = startBeat;
    note.pitch = pitch;
    note.lengthBeats = lengthBeats;
    return note;
}
} // namespace

namespace {

struct ClipLaneFixture {
    TimelineDoc doc;
    TimelineViewState state;
    ClipSelectionModel selection;
    AppUndoManager undo;
    TimelineClipLaneArea lane{state, selection};

    ClipLaneFixture() {
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        state.snap = TimelineViewState::Snap::Quarter;
        lane.setTimelineDoc(&doc);
        lane.setUndoManager(&undo);
        lane.setSize(1200, 400);
    }
};

juce::MouseEvent makeClipMouseEvent(juce::Component& comp, juce::Point<float> position, juce::ModifierKeys mods,
                                    bool mouseWasDragged, juce::Point<float> mouseDownPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos,
                            juce::Time::getCurrentTime(), 1, mouseWasDragged);
}

juce::MouseEvent leftClick(juce::Component& comp, juce::Point<float> pos, int extraFlags = 0) {
    return makeClipMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extraFlags), false,
                              pos);
}

juce::MouseEvent leftDrag(juce::Component& comp, juce::Point<float> pos, juce::Point<float> anchor,
                          int extraFlags = 0) {
    return makeClipMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extraFlags), true,
                              anchor);
}

juce::MouseEvent rightClick(juce::Component& comp, juce::Point<float> pos) {
    return makeClipMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), false, pos);
}

juce::Point<float> centreOf(juce::Rectangle<int> rect) { return {(float)rect.getCentreX(), (float)rect.getCentreY()}; }

} // namespace

namespace {

bool imagesIdentical(const juce::Image& a, const juce::Image& b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
        return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

} // namespace

namespace {

// A clip-lane fixture with a live transport and its own settings file, so the locator span and the
// preference that gates it are both real rather than stubbed.
struct LocatorSpanFixture : ClipLaneFixture {
    synth::TransportService transport;
    juce::ApplicationProperties appProperties;

    LocatorSpanFixture() {
        juce::PropertiesFile::Options options;
        options.applicationName = "TimelineClipLaneLocatorSpanTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);
        if (auto* settings = appProperties.getUserSettings())
            settings->clear();

        transport.prepare(48000.0, 512);
        lane.setTransport(&transport);
        lane.setApplicationProperties(&appProperties);
        // Snap::Off throughout, so the ONLY thing that can decide the clip's start/length is the
        // locator rule under test rather than a grid line coinciding with it.
        state.snap = TimelineViewState::Snap::Off;
    }

    ~LocatorSpanFixture() {
        if (auto* settings = appProperties.getUserSettings())
            settings->clear();
    }

    void setLocators(double startBeat, double endBeat, bool arm = true) {
        ASSERT_TRUE(transport.setLoop(startBeat, endBeat, arm));
        transport.tick(512);
    }

    void setPreference(bool enabled) {
        appProperties.getUserSettings()->setValue("timelineDoubleClickSpansLocators", enabled ? "1" : "0");
    }
};

} // namespace

namespace {

// ---- The preview-vs-commit guard (suite bug kind "preview-vs-commit") ----
// What a clip looks like on screen: its span and its notes at ABSOLUTE beats, the form a drag preview and a committed
// clip can be compared in.
struct ShownClip {
    double startBeat = 0.0;
    double lengthBeats = 0.0;
    std::vector<std::tuple<double, double, int>> notes; // absolute start, length, pitch
};

ShownClip shownClip(double startBeat, double lengthBeats, const std::vector<synth::MidiNote>& notes) {
    ShownClip shown{startBeat, lengthBeats, {}};
    for (const auto& note : notes)
        shown.notes.emplace_back(startBeat + note.startBeat, note.lengthBeats, note.pitch);
    return shown;
}

// Every clip as the lane shows it right now: a clip an edge drag is resizing (or one linked to it) shows its preview.
std::map<std::int64_t, ShownClip> shownClips(const TimelineDoc& doc, const TimelineClipLaneArea& lane) {
    std::map<std::int64_t, ShownClip> shown;
    for (const auto& track : doc.getTracks())
        for (const auto& clip : track.clips) {
            const auto preview = lane.resizePreviewFor(clip);
            shown[clip.id.value] = preview ? shownClip(preview->startBeat, preview->lengthBeats, preview->notes)
                                           : shownClip(clip.startBeat, clip.lengthBeats, clip.notes);
        }
    return shown;
}

void expectSameShownClips(const std::map<std::int64_t, ShownClip>& preview,
                          const std::map<std::int64_t, ShownClip>& committed) {
    ASSERT_EQ(preview.size(), committed.size());
    for (const auto& [id, before] : preview) {
        const auto& after = committed.at(id);
        EXPECT_NEAR(before.startBeat, after.startBeat, 1e-9) << "clip " << id;
        EXPECT_NEAR(before.lengthBeats, after.lengthBeats, 1e-9) << "clip " << id;
        ASSERT_EQ(before.notes.size(), after.notes.size()) << "clip " << id;
        for (std::size_t i = 0; i < before.notes.size(); ++i) {
            EXPECT_NEAR(std::get<0>(before.notes[i]), std::get<0>(after.notes[i]), 1e-9)
                << "clip " << id << " note " << i;
            EXPECT_NEAR(std::get<1>(before.notes[i]), std::get<1>(after.notes[i]), 1e-9)
                << "clip " << id << " note " << i;
            EXPECT_EQ(std::get<2>(before.notes[i]), std::get<2>(after.notes[i])) << "clip " << id << " note " << i;
        }
    }
}

// Drags `clipId`'s left or right edge by `beats` (40 px/beat) through the real mouse path, with `extraFlags` held,
// and expects the drop to leave every clip exactly as the last drag frame showed it. Returns the shown state.
std::map<std::int64_t, ShownClip> dragEdgeExpectingPreviewEqualsCommit(ClipLaneFixture& f, ClipId clipId, bool leftEdge,
                                                                       double beats, int extraFlags = 0) {
    const auto rect = f.lane.getClipRect(clipId);
    const juce::Point<float> edge(leftEdge ? (float)rect.getX() + 3.0f : (float)rect.getRight() - 3.0f,
                                  (float)rect.getCentreY());
    const juce::Point<float> dragged(edge.x + (float)(beats * 40.0), edge.y);
    f.lane.mouseDown(leftClick(f.lane, edge, extraFlags));
    f.lane.mouseDrag(leftDrag(f.lane, dragged, edge, extraFlags));
    const auto preview = shownClips(f.doc, f.lane);
    f.lane.mouseUp(leftDrag(f.lane, dragged, edge, extraFlags));
    const auto committed = shownClips(f.doc, f.lane);
    expectSameShownClips(preview, committed);
    return committed;
}

} // namespace
