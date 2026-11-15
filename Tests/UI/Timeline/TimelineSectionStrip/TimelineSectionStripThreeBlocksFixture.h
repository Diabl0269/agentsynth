#pragma once
// The Sections strip's motion-test fixtures: full motion on, and three sections (Intro, Verse, Bridge) over a doc the
// strip refreshes on every notification, the way the timeline panel does it.
#include "TimelineSectionStripFixture.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <cstdlib>

namespace sectionstrip_test {

struct FullMotion {
    FullMotion() {
        FadeVisibility::setAnimateOffScreenForTest(true);
        setAnimationMode(AnimationMode::full);
    }
    ~FullMotion() {
        FadeVisibility::setAnimateOffScreenForTest(false);
        setAnimationMode(AnimationMode::followSystem);
    }
};

// Intro over 0..8, Verse over 8..12, Bridge over 12..16, a clip at the start of each.
struct ThreeBlocks
    : StripFixture
    , TimelineDoc::Listener {
    PlacementId intro, verse, bridge;
    ClipId introClip, verseClip;

    ThreeBlocks() {
        introClip = doc.addClip(track, 0.0, 4.0, "Intro clip");
        verseClip = doc.addClip(track, 8.0, 4.0, "Verse clip");
        doc.addClip(track, 12.0, 4.0, "Bridge clip");
        doc.makeSection(0.0, 8.0, "Intro", 0xff56D4C0, 0.5);
        doc.makeSection(8.0, 12.0, "Verse", 0xff4FC1FF, 0.5);
        doc.makeSection(12.0, 16.0, "Bridge", 0xffE0A040, 0.5);
        strip.refresh();
        intro = doc.getPlacements()[0].id;
        verse = doc.getPlacements()[1].id;
        bridge = doc.getPlacements()[2].id;
        strip.getMotionForTest().stepForTest(0.0); // the three blocks grew in when the strip first saw them
        strip.getMotionForTest().stepForTest(600.0);
        doc.addListener(this);
    }
    ~ThreeBlocks() override { doc.removeListener(this); }
    void timelineChanged(const TimelineDoc&) override { strip.refresh(); }

    int exits() { return strip.getMotionForTest().getExitsStartedForTest(); }
    void dragBlock(double blockStart, double toBeat, int upFlags = 0) {
        const juce::Point<float> from{x(blockStart + 2.0), 12.0f}, to{x(toBeat + 2.0), 12.0f};
        strip.mouseDown(event(from, from, false));
        strip.mouseDrag(event(to, from, true));
        strip.mouseUp(event(to, from, true, upFlags));
    }
    // A drag held long enough for the block to be fully lifted when it is let go, like a real one.
    void dragBlockLifted(double blockStart, double toBeat, int upFlags = 0) {
        const juce::Point<float> from{x(blockStart + 2.0), 12.0f}, to{x(toBeat + 2.0), 12.0f};
        strip.mouseDown(event(from, from, false));
        strip.mouseDrag(event(to, from, true));
        strip.getMotionForTest().stepForTest(0.0);
        strip.getMotionForTest().stepForTest(600.0);
        strip.mouseUp(event(to, from, true, upFlags));
    }
};

constexpr int kShiftFlag = juce::ModifierKeys::shiftModifier;
constexpr int kAltFlag = juce::ModifierKeys::altModifier;
constexpr int kCmdFlag = juce::ModifierKeys::commandModifier;

// The strip as it paints now. A software image: a Windows runner reads zeros from a native one.
inline juce::Image renderStrip(TimelineSectionStrip& strip) {
    juce::Image img(juce::Image::ARGB, strip.getWidth(), strip.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(img);
    strip.paintEntireComponent(g, false);
    return img;
}

// Advances every strip clock to `ms` after the release (the tests step 16 ms frames from 0).
inline void stepStrip(TimelineSectionStrip& strip, double ms) {
    strip.getMotionForTest().stepForTest(ms);
    strip.getDropGlideForTest().stepForTest(ms);
}

// How far apart two colours are: the largest difference of any channel, 0..255.
inline int channelGap(juce::Colour a, juce::Colour b) {
    return std::max({std::abs((int)a.getRed() - (int)b.getRed()), std::abs((int)a.getGreen() - (int)b.getGreen()),
                     std::abs((int)a.getBlue() - (int)b.getBlue()), std::abs((int)a.getAlpha() - (int)b.getAlpha())});
}

} // namespace sectionstrip_test
