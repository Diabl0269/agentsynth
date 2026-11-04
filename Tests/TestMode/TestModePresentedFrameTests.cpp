// The presented frame of the stale-pixel check: rebuilt from the areas a window paints, it keeps what no repaint
// reached, as a screen does, and the comparison with a full repaint finds exactly those pixels. No native window: the
// tests hand the frame the areas a peer would paint.

#include "TestMode/TestModePresentedFrame.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::testmode::notePresentedPaint;
using synth::testmode::PresentedFrame;
using synth::testmode::ScopedOffscreenPaint;

namespace {

// A dark window with one bright block that moves without asking for a repaint.
struct Window : juce::Component {
    juce::Rectangle<int> block{10, 10, 20, 20};
    Window() { setBounds(0, 0, 100, 80); }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        g.fillRect(block);
    }
};

struct Following {
    PresentedFrame& frame = PresentedFrame::shared();
    explicit Following(Window& w) { frame.start(w); }
    ~Following() { frame.stop(); }
};

} // namespace

TEST(TestModePresentedFrame, AFreshStartEqualsAFullRepaint) {
    Window w;
    Following f(w);
    const auto d = f.frame.compareWithFullRepaint({});
    EXPECT_EQ(d.pixels, 0);
    EXPECT_EQ(d.fullRepaint.getWidth(), 100);
}

TEST(TestModePresentedFrame, PixelsNoPaintReachedStayAndAreFound) {
    Window w;
    Following f(w);
    w.block = {60, 40, 20, 20};
    f.frame.notePainted(w, {60, 40, 20, 20}); // only the new place repainted: the old block stays on screen
    const auto d = f.frame.compareWithFullRepaint({});
    EXPECT_EQ(d.pixels, 20 * 20);
    EXPECT_EQ(d.bounds, juce::Rectangle<int>(10, 10, 20, 20));
    f.frame.notePainted(w, {10, 10, 20, 20}); // the old place too: nothing left
    EXPECT_EQ(f.frame.compareWithFullRepaint({}).pixels, 0);
}

TEST(TestModePresentedFrame, AnIgnoredAreaDoesNotCount) {
    Window w;
    Following f(w);
    w.block = {60, 40, 20, 20};
    f.frame.notePainted(w, {60, 40, 20, 20});
    juce::RectangleList<int> ignore;
    ignore.add({0, 0, 50, 50});
    EXPECT_EQ(f.frame.compareWithFullRepaint(ignore).pixels, 0);
}

TEST(TestModePresentedFrame, PaintsOffScreenOrOfAnotherWindowAreNotPresented) {
    Window w, other;
    Following f(w);
    w.block = {60, 40, 20, 20};
    {
        const ScopedOffscreenPaint offscreen; // a screenshot paints everything, the screen gets none of it
        f.frame.notePainted(w, w.getLocalBounds());
    }
    f.frame.notePainted(other, other.getLocalBounds());
    EXPECT_EQ(f.frame.compareWithFullRepaint({}).pixels, 2 * 20 * 20);
}

TEST(TestModePresentedFrame, TheWindowsOwnPaintHandsItsClipToTheFrame) {
    Window w;
    Following f(w);
    w.block = {60, 40, 20, 20};
    juce::Image target(juce::Image::ARGB, 100, 80, true);
    juce::Graphics g(target);
    g.reduceClipRegion(juce::Rectangle<int>(0, 0, 50, 50)); // the old place only
    notePresentedPaint(w, g);
    EXPECT_EQ(f.frame.compareWithFullRepaint({}).bounds, juce::Rectangle<int>(60, 40, 20, 20));
}

TEST(TestModePresentedFrame, DifferencesAreMarkedOnTheFullRepaint) {
    Window w;
    Following f(w);
    w.block = {60, 40, 20, 20};
    const auto before = f.frame.image().createCopy();
    const auto d = f.frame.compareWithFullRepaint({});
    const auto marked = PresentedFrame::markDifferences(before, d.fullRepaint, {});
    EXPECT_EQ(marked.getPixelAt(15, 15), juce::Colours::magenta);
    EXPECT_EQ(marked.getPixelAt(65, 45), juce::Colours::magenta);
    EXPECT_EQ(marked.getPixelAt(45, 5).getARGB(), juce::Colours::black.getARGB());
}

TEST(TestModePresentedFrame, AResizedWindowStartsAgainFromAFullRepaint) {
    Window w;
    Following f(w);
    w.block = {60, 40, 20, 20};
    w.setSize(120, 80);
    f.frame.notePainted(w, {0, 0, 1, 1});
    EXPECT_EQ(f.frame.image().getWidth(), 120);
    EXPECT_EQ(f.frame.compareWithFullRepaint({}).pixels, 0);
}
