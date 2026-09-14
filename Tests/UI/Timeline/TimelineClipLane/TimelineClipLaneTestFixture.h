// TimelineClipLaneTestFixture.h — shared fixture/helpers for the TimelineClipLane test split.
#pragma once

#include "AppUndoManager.h"
#include "Modules/RecordTapModule.h"
#include "Timeline/TimelineDoc.h"
#include "Transport/TransportService.h"
#include "UI/Timeline/ClipSelectionModel.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include "UI/Timeline/TimelineViewState.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

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
