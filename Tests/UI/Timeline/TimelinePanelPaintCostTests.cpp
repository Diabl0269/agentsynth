// TimelinePanelPaintCostTests.cpp
//
// A measurement, not a gate: what one Smooth follow frame costs to paint when the whole panel is repainted. Run with
// --gtest_also_run_disabled_tests --gtest_filter=TimelinePanelPaintCost.* and read the printed median and p95.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

TEST(TimelinePanelPaintCost, DISABLED_ZoomedInFollowFrame) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1600, 900);
    panel.setTimelineDoc(&doc);

    for (int t = 0; t < 8; ++t) {
        const bool audio = t % 2 == 1;
        const auto track = doc.addTrack(audio ? synth::TrackKind::Audio : synth::TrackKind::Midi,
                                        (audio ? "Audio " : "Midi ") + juce::String(t));
        for (int c = 0; c < 24; ++c) {
            const auto clip = doc.addClip(track, c * 4.0, 3.5, "Clip " + juce::String(c));
            if (audio) {
                doc.setClipAsset(clip, "Audio/take-" + juce::String(c) + ".wav", 0.0);
                continue;
            }
            for (int n = 0; n < 24; ++n) {
                synth::MidiNote note;
                note.startBeat = n * 0.125 * 4.0 / 3.0;
                note.lengthBeats = 0.25;
                note.pitch = 48 + (n * 7) % 24;
                doc.addNote(clip, note);
            }
        }
    }
    panel.getViewState().pixelsPerBeat = 120.0; // zoomed in: a couple of clips fill the width
    panel.getViewState().firstVisibleBeat = 40.0;
    panel.getPlayhead().setVisible(false);

    juce::Image image(juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true, juce::SoftwareImageType());
    std::vector<double> millis;
    for (int i = 0; i < 200; ++i) {
        const double start = juce::Time::getMillisecondCounterHiRes();
        juce::Graphics g(image);
        panel.paintEntireComponent(g, true);
        millis.push_back(juce::Time::getMillisecondCounterHiRes() - start);
    }
    std::sort(millis.begin(), millis.end());
    const double median = millis[millis.size() / 2];
    const double p95 = millis[(size_t)((double)millis.size() * 0.95)];
    std::printf("PaintCost ZoomedInFollowFrame (1600x900, 8 tracks, 200 paints): median %.3f ms, p95 %.3f ms\n", median,
                p95);
    EXPECT_GT(median, 0.0);
}
