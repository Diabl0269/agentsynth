#pragma once
// The Sections strip's test fixture: a doc, a view at 40 px per beat, an undo manager and a strip that records its
// menus instead of showing them, with helpers that synthesise MouseEvents on the strip's real handlers.
#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"
#include "UI/Timeline/TimelineViewState.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace sectionstrip_test {
using namespace synth;
using namespace synth::ui;

// Records the menus instead of showing them: a real PopupMenu needs a display the CI runner lacks.
class RecordingStrip : public TimelineSectionStrip {
public:
    using TimelineSectionStrip::TimelineSectionStrip;
    int blockMenus = 0;
    int stripMenus = 0;

protected:
    void openSectionContextMenu(PlacementId) override { ++blockMenus; }
    void openStripContextMenu() override { ++stripMenus; }
};

struct StripFixture {
    TimelineDoc doc;
    TimelineViewState state;
    AppUndoManager undo;
    RecordingStrip strip{state};
    std::vector<ClipId> selectedClips;
    TrackId track;

    StripFixture() {
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        strip.setTimelineDoc(&doc);
        strip.setUndoManager(&undo);
        strip.setSize(1200, 24);
        strip.onSelectClips = [this](const std::vector<ClipId>& ids) { selectedClips = ids; };
        track = doc.addTrack(TrackKind::Midi, "Lead");
    }

    juce::MouseEvent event(juce::Point<float> pos, juce::Point<float> down, bool dragged, int flags = 0) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | flags), 0.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, &strip, &strip, juce::Time::getCurrentTime(), down,
                                juce::Time::getCurrentTime(), 1, dragged);
    }
    float x(double beat) const { return (float)state.beatToX(beat); }

    void dragAcross(double fromBeat, double toBeat) {
        const juce::Point<float> from{x(fromBeat), 12.0f}, to{x(toBeat), 12.0f};
        strip.mouseDown(event(from, from, false));
        strip.mouseDrag(event(to, from, true));
        strip.mouseUp(event(to, from, true));
    }

    // One section over bars 1..2 (beats 0..8) and a second placement of it at beat 16.
    PlacementId makeRepeatedSection() {
        doc.addClip(track, 0.0, 4.0, "A");
        const auto id = doc.makeSection(0.0, 8.0, "Chorus", 0xff56D4C0, 0.5);
        doc.placeSection(id, 16.0, SectionDropMode::MakeRoom, 0.5);
        strip.refresh();
        return doc.getPlacements().front().id;
    }
};

} // namespace sectionstrip_test
