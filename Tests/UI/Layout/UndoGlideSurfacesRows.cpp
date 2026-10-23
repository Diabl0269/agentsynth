// UndoGlideSurfacesRows.cpp -- the registry fixtures for the rows that already glide on undo: timeline track header
// rows and automation lane rows (docs/layout/animation.md "Undo and redo glide").
#include "../Timeline/AutomationLanes/AutomationLanesMenuFixture.h"
#include "../Timeline/TimelinePanel/TimelinePanelTestFixture.h"
#include "BottomDockActiveTabResetGuard.h"
#include "FadeVisibilityTestGuard.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UndoGlideSurfaces.h"

namespace undo_glide_test {

namespace {

/** Track header rows: a track dragged to the bottom, then restored. The glide is clock-driven, so stepping sleeps. */
class TrackRowSurface : public Surface {
public:
    TrackRowSurface() {
        mc_.setSize(1400, 900);
        mc_.newPatchForTest();
        for (int i = 0; i < 3; ++i)
            mc_.simulateAddAudioTrackClick();
        panel_ = &mc_.getTimelinePanel();
        panel_->setSize(1400, 700);
        for (const auto& track : mc_.getTimelineDoc().getTracks())
            first_ = first_.isValid() ? first_ : track.id;
        panel_->forceTrackGlideForTest(true);
    }

    void move() override {
        auto& doc = mc_.getTimelineDoc();
        mc_.getUndoManager().recordTimelineChange(doc, [&] { doc.moveTrack(first_, 2); });
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override {
        for (int i = 0; i < panel_->getTrackHeaderCount(); ++i)
            if (panel_->getTrackHeaderAt(i)->getTrackId() == first_)
                return panel_->getTrackHeaderAt(i)->getY();
        return -1.0;
    }
    bool gliding() const override { return panel_->isTrackReorderActiveForTest(); }
    void stepTo(double elapsedMs) override {
        const double target = std::min(elapsedMs, 250.0);
        if (target > sleptMs_)
            juce::Thread::sleep((int)(target - sleptMs_));
        sleptMs_ = std::max(sleptMs_, target);
        panel_->advanceTrackGlideForTest();
    }

private:
    bool restore(bool forward) {
        sleptMs_ = 0.0;
        return forward ? mc_.getUndoManager().redo() : mc_.getUndoManager().undo();
    }

    BottomDockActiveTabResetGuardMDT resetGuard_;
    MainComponent mc_{std::make_unique<MockProviderTL>()};
    synth::ui::TimelinePanelComponent* panel_ = nullptr;
    synth::TrackId first_;
    double sleptMs_ = 0.0;
};

juce::KeyPress commandAlt(int keyCode) {
    return juce::KeyPress(keyCode,
                          juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier), 0);
}

/** Automation lane rows: a lane moved down with Cmd+Alt+Down, then restored, on a pinned glide clock. */
class LaneRowSurface : public Surface {
public:
    LaneRowSurface() {
        bass_ = scene_.doc.addTrack(synth::TrackKind::Midi, "Bass");
        cutoff_ = scene_.addLane(bass_, "cutoff");
        scene_.addLane(bass_, "res");
        scene_.panel.setTrackHeaderHost(&host_);
        scene_.panel.setTrackAutomationExpanded(bass_, true);
        lanes().setLaneGlideClockForTest([this] { return nowMs_; });
    }
    ~LaneRowSurface() override {
        lanes().setLaneGlideClockForTest({});
        scene_.panel.setTrackHeaderHost(nullptr);
    }

    void move() override {
        scene_.panel.laneHeaderForTest(cutoff_)->keyPressed(commandAlt(juce::KeyPress::downKey));
        nowMs_ += 1000.0;
        lanes().stepLaneGlideForTest();
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override { return scene_.panel.laneHeaderForTest(cutoff_)->getY(); }
    bool gliding() const override { return const_cast<LaneRowSurface*>(this)->lanes().isLaneReorderActive(); }
    void stepTo(double elapsedMs) override {
        nowMs_ = startMs_ + elapsedMs;
        lanes().stepLaneGlideForTest();
    }

private:
    synth::ui::TimelineAutomationLanes& lanes() { return scene_.panel.getAutomationLanes(); }
    bool restore(bool forward) {
        startMs_ = nowMs_;
        return forward ? scene_.undo.redo() : scene_.undo.undo();
    }

    FadeAnimateGuard animate_;
    mutable automation_lanes_test::LanesPanel scene_;
    lane_menu_test::PickHost host_;
    synth::TrackId bass_;
    synth::LaneId cutoff_;
    double nowMs_ = 1000.0;
    double startMs_ = 1000.0;
};

} // namespace

std::unique_ptr<Surface> makeTimelineTrackRows() { return std::make_unique<TrackRowSurface>(); }

std::unique_ptr<Surface> makeAutomationLaneReorder() { return std::make_unique<LaneRowSurface>(); }

} // namespace undo_glide_test
