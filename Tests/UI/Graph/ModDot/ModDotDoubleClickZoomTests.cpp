// The double-click-to-disconnect gestures at every canvas zoom: presses are hit-tested through the real component
// hierarchy at screen positions (the canvas content is scaled by the zoom), then delivered to the component under
// the pointer in its own frame, exactly as the OS path does. docs/modules/modulation.md#the-mod-dot-menu.

#include "ModDotTestFixture.h"

#include "Modules/OscillatorModule.h"
#include "Project/ViewDoc.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

namespace {

constexpr float kZooms[] = {0.5f, 1.0f, 2.0f};

struct NoMotion {
    NoMotion() { synth::ui::setReducedMotionForTest(true); }
    ~NoMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// Zooms the canvas and pans it so `card` sits well inside an editor large enough to show it at every zoom.
void zoomTo(GraphEditor& editor, ModuleComponent& card, float zoom) {
    editor.setVisible(true); // getComponentAt only descends into visible components
    editor.setBounds(0, 0, 1600, 1200);
    synth::ViewDoc view;
    view.zoom = zoom;
    view.panX = 100.0f - static_cast<float>(card.getX()) * zoom;
    view.panY = 100.0f - static_cast<float>(card.getY()) * zoom;
    editor.applyViewDoc(view);
    editor.settleZoomNowForTest();
}

// A real press and release at `screen` (a point in the editor's frame): the component under it, its own local
// position, and the click count the OS reports for the second press of a double-click.
void pressAndRelease(GraphEditor& editor, juce::Point<float> screen, int clicks) {
    auto* hit = editor.getComponentAt(screen.roundToInt());
    ASSERT_NE(hit, nullptr);
    const auto local = hit->getLocalPoint(&editor, screen);
    const auto at = juce::Time::getCurrentTime();
    auto event = [&](int numClicks) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), local, kPlain, 0.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, hit, hit, at, local, at, numClicks, false);
    };
    hit->mouseDown(event(clicks));
    hit->mouseUp(event(clicks));
}

// A user never lands on the exact pixel: both presses are this many screen pixels off the target's centre.
constexpr float kOffTargetPx = 5.0f;

void doubleClickAt(GraphEditor& editor, juce::Point<float> screen, juce::Point<float> direction = {1.0f, 0.0f}) {
    screen += direction * kOffTargetPx;
    pressAndRelease(editor, screen, 1);
    pressAndRelease(editor, screen, 2);
}

} // namespace

TEST_F(ModuleComponentTest, DoubleClickingAConnectedAudioJackDisconnectsItAtEveryZoom) {
    NoMotion motion;
    for (const float zoom : kZooms) {
        Fixture f(false);
        auto osc = f.engine.getGraph().addNode(std::make_unique<OscillatorModule>());
        ASSERT_TRUE(f.engine.getGraph().addConnection({{osc->nodeID, 0}, {f.vcaId, 0}}));
        f.refresh();
        zoomTo(*f.editor, *f.vcaCard, zoom);
        const auto jack = f.vcaCard->getPortCenter(0, true);

        doubleClickAt(*f.editor, f.editor->getLocalPoint(f.vcaCard, jack.toFloat()));

        EXPECT_FALSE(f.engine.getGraph().isConnected({{osc->nodeID, 0}, {f.vcaId, 0}})) << "zoom " << zoom;
    }
}

TEST_F(ModuleComponentTest, DoubleClickingAModulatedKnobsDotRemovesItsSourceAtEveryZoom) {
    NoMotion motion;
    for (const float zoom : kZooms) {
        Fixture f;
        zoomTo(*f.editor, *f.vcaCard, zoom);
        const auto dot = f.vcaCard->getModTargetKnobAnchor(f.gainChannel);
        ASSERT_TRUE(dot.has_value());

        // Off the dot sideways, along the ring, so the press stays on the knob.
        const auto knobCentre =
            f.vcaCard->getLocalPoint(f.gainKnob, f.gainKnob->getLocalBounds().getCentre().toFloat());
        auto outward = *dot - knobCentre;
        outward = outward / juce::jmax(0.01f, outward.getDistanceFromOrigin());
        const juce::Point<float> sideways(-outward.y, outward.x);

        doubleClickAt(*f.editor, f.editor->getLocalPoint(f.vcaCard, *dot), sideways);

        EXPECT_TRUE(synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel, true).empty()) << "zoom " << zoom;
    }
}

TEST_F(ModuleComponentTest, DoubleClickingAVisibleCvJackRemovesItsChainAtEveryZoom) {
    NoMotion motion;
    for (const float zoom : kZooms) {
        Fixture f(true, false);
        zoomTo(*f.editor, *f.vcaCard, zoom);
        auto* mb = dynamic_cast<ModuleBase*>(f.vcaCard->getModule());
        const int cvJack = mb->mapInputChannel(f.gainChannel).visibleJackIndex;

        doubleClickAt(*f.editor, f.editor->getLocalPoint(f.vcaCard, f.vcaCard->getPortCenter(cvJack, true).toFloat()));

        EXPECT_TRUE(synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel, true).empty()) << "zoom " << zoom;
    }
}
