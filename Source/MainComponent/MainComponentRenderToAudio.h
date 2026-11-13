#pragma once

// "Render to audio track" (docs/timeline/render-to-audio.md): renders a range, or the selected clips' spans, of MIDI
// and Audio tracks that reach a mixer channel to new Audio tracks. A friend of MainComponent, so each function takes
// the component as its first parameter. Message thread only.

#include "UI/Timeline/RenderSpan.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth {
struct StemResult;
}

class MainComponent;

struct MainComponentRenderToAudio {
    /** The tracks among `tracks` this can render: MIDI or Audio tracks whose sound reaches a mixer channel. */
    static std::vector<synth::TrackId> renderableTracks(MainComponent& mc, const std::vector<synth::TrackId>& tracks);

    /** Starts the render of `spans` and returns true, or refuses with a status-bar message and returns false (a take
     *  or an export is running, or no span has a mixer channel). The tracks are added when the render completes, so
     *  callers watch isExportAvailable() for the end. */
    static bool request(MainComponent& mc, const std::vector<synth::ui::RenderSpan>& spans);

    // ---- The steps of a render, members only so they share the friendship ----
    struct Item;
    struct Plan;
    using Safe = juce::Component::SafePointer<MainComponent>;
    using Window = juce::Component::SafePointer<juce::DialogWindow>;
    static std::vector<Item> resolveItems(MainComponent& mc, const std::vector<synth::ui::RenderSpan>& spans);
    static bool placeRenderedFiles(MainComponent& mc, Plan& plan, const synth::StemResult& result);
    static int addRenderedTracks(MainComponent& mc, const Plan& plan);
    static bool addRenderedTrack(MainComponent& mc, const Item& item);
    static void commitRender(Safe safe, std::shared_ptr<Plan> plan);
    static void finishRender(Safe safe, Window window, std::shared_ptr<Plan> plan, const synth::StemResult& result);
    static Window openProgressWindow(MainComponent& mc);

    /** Test seam: false skips the modal progress window, which a display-less runner cannot open. */
    static void setProgressDialogEnabledForTest(bool enabled);
    /** Test seam: what the progress window's Cancel button does. */
    static void cancelForTest(MainComponent& mc);
    static bool isIdleForTest(const MainComponent& mc);
};
