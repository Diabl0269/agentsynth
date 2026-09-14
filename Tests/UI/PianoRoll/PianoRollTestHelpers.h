#pragma once

// Shared fixture and helpers for the PianoRoll test suite (Tests/UI/PianoRoll/PianoRoll*Tests.cpp).
// Header-only; not compiled on its own and not registered in Tests/CMakeLists.txt.

#include "AppUndoManager.h"
#include "ShortcutManager/ShortcutManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include "UI/PianoRoll/ScaleAssistPanel.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineViewState.h"
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

using synth::ClipId;
using synth::NoteId;
using synth::TimelineDoc;
using synth::TrackKind;
using synth::ui::EditTool;
using synth::ui::NoteSelectionModel;
using synth::ui::PianoRollComponent;
using synth::ui::ScaleAssistPanel;
using synth::ui::TimelineViewState;

inline NoteId nid(std::int64_t v) { return NoteId{v}; }

inline synth::MidiNote makeNote(double startBeat, int pitch, double lengthBeats = 1.0) {
    synth::MidiNote note;
    note.startBeat = startBeat;
    note.pitch = pitch;
    note.lengthBeats = lengthBeats;
    return note;
}

// Counts every repaint the roll's LOCAL playhead asks for — the same paint-count pattern
// TimelinePlayheadTests.cpp uses on the overlay, applied to the roll's own seam. The Split tool's
// hover preview has its OWN counter (its own seam), so a test can prove a hover repaints the
// preview strip and NOTHING else.
struct CountingRoll : PianoRollComponent {
    using PianoRollComponent::PianoRollComponent;

    int requests = 0;
    juce::Rectangle<int> lastStrip;
    int previewRequests = 0;
    juce::Rectangle<int> lastPreviewStrip;
    int headerButtonRequests = 0;
    juce::Rectangle<int> lastHeaderButtonStrip;

    void requestRepaintStrip(juce::Rectangle<int> strip) override {
        ++requests;
        lastStrip = strip;
        PianoRollComponent::requestRepaintStrip(strip);
    }

    void requestRepaintPreviewStrip(juce::Rectangle<int> strip) override {
        ++previewRequests;
        lastPreviewStrip = strip;
        PianoRollComponent::requestRepaintPreviewStrip(strip);
    }

    void requestRepaintHeaderButtonStrip(juce::Rectangle<int> strip) override {
        ++headerButtonRequests;
        lastHeaderButtonStrip = strip;
        PianoRollComponent::requestRepaintHeaderButtonStrip(strip);
    }

    // The clip-overrun prompt's seam. Deliberately does NOT call the base implementation: that one
    // opens a real juce::AlertWindow, and a headless run has no message loop to answer it with (nor
    // any business creating a window). It records BOTH halves of the request — the required length
    // and the CLIP ID the prompt was raised for — because the captured id is what routes the answer,
    // and a test that only saw the length could not tell a correctly-routed Extend from one that grew
    // whichever clip happened to be open. Whichever arm a test wants to exercise, it then drives
    // applyExtendPromptAnswer(), which is exactly what the real alert callback calls.
    int extendPrompts = 0;
    double lastExtendPromptRequest = 0.0;
    ClipId lastExtendPromptClipId;
    void promptExtendClipToFitNotes(ClipId clipId, double requiredLengthBeats) override {
        ++extendPrompts;
        lastExtendPromptClipId = clipId;
        lastExtendPromptRequest = requiredLengthBeats;
    }
};

struct PianoRollFixture {
    TimelineDoc doc;
    TimelineViewState state;
    AppUndoManager undo;
    CountingRoll roll{state};

    PianoRollFixture() {
        // The SHARED view state is deliberately left at a different zoom/origin from the roll's own
        // mapping below: every assertion here that goes through the roll must use the roll's
        // beatToX/xToBeat, never this one (which is now consulted for the snap division alone).
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        state.snap = TimelineViewState::Snap::Quarter;
        roll.setTimelineDoc(&doc);
        roll.setUndoManager(&undo);
        roll.setSize(900, 160);
    }

    // Opens the roll and PINS its own horizontal mapping, so a pixel offset in a test means an
    // exact number of beats (openClip zooms to fit, which is clip-length dependent by design).
    void open(ClipId id, double pixelsPerBeat = 40.0) {
        roll.openClip(id);
        const auto* clip = doc.getClip(id);
        roll.setHorizontalView(pixelsPerBeat, clip != nullptr ? clip->startBeat : 0.0);
    }
};

inline juce::MouseEvent makeRollMouseEvent(juce::Component& comp, juce::Point<float> position, juce::ModifierKeys mods,
                                           bool mouseWasDragged, juce::Point<float> mouseDownPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos,
                            juce::Time::getCurrentTime(), 1, mouseWasDragged);
}

inline juce::MouseEvent leftClick(juce::Component& comp, juce::Point<float> pos, int extraFlags = 0) {
    return makeRollMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extraFlags), false,
                              pos);
}

inline juce::MouseEvent leftDrag(juce::Component& comp, juce::Point<float> pos, juce::Point<float> anchor,
                                 int extraFlags = 0) {
    return makeRollMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extraFlags), true,
                              anchor);
}

inline juce::Point<float> centreOf(juce::Rectangle<int> rect) {
    return {(float)rect.getCentreX(), (float)rect.getCentreY()};
}

// A no-button move: what mouseMove/mouseEnter get, as opposed to leftClick's pressed state.
inline juce::MouseEvent hover(juce::Component& comp, juce::Point<float> pos) {
    return makeRollMouseEvent(comp, pos, juce::ModifierKeys(), false, pos);
}

// A point inside the row for `pitch` at the given ABSOLUTE beat, through the roll's own mapping.
// +5 lands in the middle of a 10 px row (kPixelsPerSemitone), so a click can never fall on the
// boundary between two rows.
inline juce::Point<float> pointAt(PianoRollComponent& roll, double absBeat, int pitch) {
    return {(float)roll.beatToX(absBeat), (float)roll.yForPitch(pitch) + 5.0f};
}

// Pins snap explicitly — never inherited from the fixture or from a machine-local preference. Every
// test across the suite that is grid-sensitive configures snap explicitly (never inherits a
// default), because a machine-local snap preference must never be able to decide whether it passes.
// The wheel/zoom tests pin the view instead (zoom and scroll origin, via PianoRollFixture::open) for
// the same reason: a wheel assertion measured in beats depends on pixels-per-beat, so it is stated
// rather than inherited.
inline void setSnap(PianoRollFixture& f, TimelineViewState::Snap snap, bool enabled = true) {
    f.state.snap = snap;
    f.state.snapEnabled = enabled;
}

// The selection is a single note, and it is this one — the assertion every navigation test ends on,
// because "collapses to a single note" and "picks the right one" are one claim here, not two.
//
// Returns an AssertionResult rather than EXPECT-ing inline so a caller can attach its own "why THIS
// note" rationale with <<, and so a failure names the ids involved instead of pointing at this
// helper's line.
inline ::testing::AssertionResult onlySelected(PianoRollFixture& f, NoteId expected) {
    const auto selected = f.roll.getSelectionForTest().getSelected();
    if (selected.size() != 1u)
        return ::testing::AssertionFailure() << "expected exactly one selected note, got " << selected.size()
                                             << " (navigation must COLLAPSE a multi-selection)";
    if (selected[0] != expected)
        return ::testing::AssertionFailure()
               << "selected note is id " << selected[0].value << ", expected id " << expected.value;
    return ::testing::AssertionSuccess();
}

inline juce::KeyPress altArrow(int keyCode) { return juce::KeyPress(keyCode, juce::ModifierKeys::altModifier, 0); }

// Picks the first built-in preset ("Major", combo id 2 — see ScaleAssistPanel::rebuildScaleCombo)
// for whichever clip is open, which is how the panel, the chip and the key all learn the scale.
inline void chooseMajorScaleForOpenClip(PianoRollFixture& f) {
    f.roll.getScaleAssistPanel().getScaleCombo().setSelectedId(2, juce::sendNotificationSync);
}

inline juce::MouseWheelDetails wheelOnY(float deltaY) {
    juce::MouseWheelDetails wheel{}; // value-initialised: the struct has no default member initialisers
    wheel.deltaY = deltaY;
    return wheel;
}

// C major, root C (pitch class 0): C D E F G A B.
inline bool cMajorContains(int pitch) {
    static const bool kInScale[12] = {true, false, true, false, true, true, false, true, false, true, false, true};
    return kInScale[(size_t)(((pitch % 12) + 12) % 12)];
}

// Plain Euclidean RGB distance — same idiom as NoteColourTests.cpp's rgbDistance.
inline double rgbDistance(juce::Colour a, juce::Colour b) {
    const double dr = (double)a.getRed() - (double)b.getRed();
    const double dg = (double)a.getGreen() - (double)b.getGreen();
    const double db = (double)a.getBlue() - (double)b.getBlue();
    return std::sqrt(dr * dr + dg * dg + db * db);
}

// A throwaway temp file, never the real user settings — the scale-assist persistence tests below
// (panel visibility + user scales) need a REAL juce::PropertiesFile (setPropertiesFile takes a
// pointer to one), not the ApplicationProperties wrapper the Preferences-tab tests use.
inline std::unique_ptr<juce::PropertiesFile> makeScaleAssistTestProps(const juce::String& name) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name + ".settings");
    file.deleteFile();
    juce::PropertiesFile::Options opts;
    opts.applicationName = name;
    opts.filenameSuffix = "settings";
    return std::make_unique<juce::PropertiesFile>(file, opts);
}

// Records auditionTrackNote and nothing else; every other TrackHeaderHost member is an inert stub
// (used to test the audition path, not the track-header column).
class AuditionRecordingHost : public synth::ui::TrackHeaderHost {
public:
    struct Call {
        synth::TrackId track;
        int pitch = 0;
        int velocity = 0;
        bool on = false;
    };

    void auditionTrackNote(synth::TrackId track, int pitch, int velocity, bool noteOn) override {
        calls.push_back({track, pitch, velocity, noteOn});
    }

    std::vector<Call> onCalls() const {
        std::vector<Call> out;
        for (const auto& c : calls)
            if (c.on)
                out.push_back(c);
        return out;
    }
    std::vector<Call> offCalls() const {
        std::vector<Call> out;
        for (const auto& c : calls)
            if (!c.on)
                out.push_back(c);
        return out;
    }

    std::vector<Call> calls;

    // ---- inert stubs ----
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(synth::TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
};

// A real TimelinePanelComponent with the roll open on one clip holding one note, plus the recording
// host — i.e. exactly the production wiring, minus the graph. Used both by the audition-path tests
// and by the layout test proving the FIRST clip opened is framed against the real canvas top.
struct AuditionIntegrationFixture {
    TimelineDoc doc;
    AppUndoManager undo;
    AuditionRecordingHost host;
    synth::ui::TimelinePanelComponent panel;
    synth::TrackId trackId;
    ClipId clipId;
    NoteId noteId;

    AuditionIntegrationFixture() {
        panel.setSize(1200, 320);
        auto& state = panel.getViewState();
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        state.snap = TimelineViewState::Snap::Quarter;
        state.snapEnabled = true;
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        panel.setTrackHeaderHost(&host);

        trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        clipId = doc.addClip(trackId, 0.0, 8.0, "Clip");
        noteId = doc.addNote(clipId, makeNote(1.0, 64, 1.0));
        panel.openPianoRoll(clipId);
    }

    synth::ui::PianoRollComponent& roll() { return panel.getPianoRoll(); }

    // Presses (and holds) the note — the on edge travels the real chain.
    void pressAndHoldNote() {
        auto& r = roll();
        const auto rect = r.getNoteRect(noteId);
        r.mouseDown(leftClick(r, centreOf(rect)));
    }
};
