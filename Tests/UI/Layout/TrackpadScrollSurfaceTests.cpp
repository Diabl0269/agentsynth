// A trackpad swipe on every scroll surface that is not the timeline's own vertical scroll (that one has
// TimelinePanelTrackpadScrollTests): the piano roll, the drum machine, the canvas pan, the timeline's sideways scroll
// and the shared viewport behind the mixer's lists. Per surface: an even step per display frame at 60 and 120 Hz with
// one view move per frame, a fling that runs into an end stops there and never moves back, and a swipe into an end it
// already sits on moves nothing and announces nothing.

#include "../Graph/GraphEditor/GraphEditorTestHelpers.h"
#include "../PianoRoll/PianoRollTestHelpers.h"
#include "../Timeline/TimelinePanel/TimelinePanelTestEvents.h"
#include "TrackpadSwipeHarness.h"
#include "UI/DrumMachine/DrumMachineComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"

using namespace trackpad_test;

namespace {

juce::MouseWheelDetails wheelOf(float dx, float dy, bool inertial) {
    juce::MouseWheelDetails w{};
    w.deltaX = dx;
    w.deltaY = dy;
    w.isSmooth = true;
    w.isInertial = inertial;
    return w;
}

template <typename Comp>
std::function<void(float, bool)> sender(Comp& c, bool horizontal) {
    return [&c, horizontal](float d, bool inertial) {
        c.mouseWheelMove(makeClickEvent(c, {100.0f, 60.0f}),
                         horizontal ? wheelOf(d, 0.0f, inertial) : wheelOf(0.0f, d, inertial));
    };
}

struct RollSurface {
    PianoRollFixture f;
    int told = 0;
    Surface s;
    RollSurface(bool horizontal) {
        const auto track = f.doc.addTrack(TrackKind::Midi, "Track 1");
        f.open(f.doc.addClip(track, 0.0, 64.0, "Clip"), 40.0);
        f.roll.getWheelForTest().setOffScreenForTest(true);
        f.roll.onHorizontalViewChanged = [this] { ++told; };
        s.router = &f.roll.getWheelForTest();
        s.axis = horizontal ? 0 : 1;
        s.send = sender(f.roll, horizontal);
        s.position = [this, horizontal] {
            return horizontal ? f.roll.getFirstVisibleBeat() : f.roll.getTopRowPositionForTest();
        };
        if (horizontal)
            s.notifications = [this] { return told; };
    }
};

struct DrumSurface {
    TimelineDoc doc;
    AppUndoManager undo;
    synth::ui::DrumMachineComponent grid;
    int told = 0;
    Surface s;
    DrumSurface(bool horizontal) {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        const auto track = doc.addTrack(TrackKind::Midi, "Drums");
        const auto clip = doc.addClip(track, 0.0, 8.0, "Beat 1");
        grid.setTimelineDoc(&doc);
        grid.setUndoManager(&undo);
        grid.setSize(1000, 50);
        grid.openClip(clip);
        grid.getWheelForTest().setOffScreenForTest(true);
        grid.onHorizontalViewChanged = [this] { ++told; };
        s.router = &grid.getWheelForTest();
        s.axis = horizontal ? 0 : 1;
        s.send = sender(grid, horizontal);
        s.position = [this, horizontal] {
            return horizontal ? grid.getViewState().firstVisibleBeat : grid.getScrollYForTest();
        };
        if (horizontal)
            s.notifications = [this] { return told; };
    }
};

struct TimelineXSurface {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    Surface s;
    TimelineXSurface() {
        panel.setTimelineDoc(&doc);
        panel.setSize(1400, 500);
        const auto id = doc.addTrack(synth::TrackKind::Midi, "Track");
        doc.addClip(id, 0.0, 6.0, "Clip");
        panel.getWheelForTest().setOffScreenForTest(true);
        s.router = &panel.getWheelForTest();
        s.axis = 0;
        s.send = sender(panel, true);
        s.position = [this] { return panel.getViewState().firstVisibleBeat; };
    }
};

template <typename Fixture>
void runAll(bool horizontal) {
    {
        std::unique_ptr<Fixture> f;
        checkEvenAtBothRates([&]() -> Surface& {
            f = std::make_unique<Fixture>(horizontal);
            return f->s;
        });
    }
    {
        Fixture f(horizontal);
        checkCoastsToAStopAtTheEnd(f.s);
    }
    {
        Fixture f(horizontal);
        checkNoMoveNoNotification(f.s);
    }
}

} // namespace

TEST(TrackpadScrollSurfaceTest, PianoRollHorizontal) { runAll<RollSurface>(true); }
TEST(TrackpadScrollSurfaceTest, PianoRollVertical) { runAll<RollSurface>(false); }
TEST(TrackpadScrollSurfaceTest, DrumMachineHorizontal) { runAll<DrumSurface>(true); }
TEST(TrackpadScrollSurfaceTest, DrumMachineVertical) { runAll<DrumSurface>(false); }

TEST(TrackpadScrollSurfaceTest, TimelineSidewaysScroll) {
    std::unique_ptr<TimelineXSurface> f;
    checkEvenAtBothRates([&]() -> Surface& {
        f = std::make_unique<TimelineXSurface>();
        return f->s;
    });
    TimelineXSurface a;
    checkCoastsToAStopAtTheEnd(a.s);
    TimelineXSurface b;
    checkNoMoveNoNotification(b.s);
}

TEST_F(GraphEditorTest, CanvasPanIsEvenAtBothRatesAndOneMovePerFrame) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    for (const bool horizontal : {true, false}) {
        std::unique_ptr<Surface> s;
        auto make = [&]() -> Surface& {
            editor.centreViewOn({0.0f, 0.0f});
            editor.viewFlyForTest().setOffScreenForTest(true);
            s = std::make_unique<Surface>();
            s->router = &editor.viewFlyForTest();
            s->axis = horizontal ? 0 : 1;
            s->send = sender(editor, horizontal);
            s->position = [&editor, horizontal] {
                const auto r = editor.getVisibleCanvasRect();
                return (double)(horizontal ? r.getX() : r.getY());
            };
            return *s;
        };
        checkEvenAtBothRates(make);
    }
}
