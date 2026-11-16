// UndoGlideSurfacesColourTrack.cpp -- the registry fixtures for a track's colour changed through the colour picker's
// commit (one undo step) and undone: the timeline header's stripe, the clips in the clip lane, the mixer strip's dot,
// the automation lane's stripe and the modulator row's tag. Every surface that shows the colour glides in step, each on
// its own tween. drawn() is the red channel (0..255) of the colour as drawn; the two colours differ in red by
// construction (docs/layout/undo-glide.md).
#include "../../TestWait.h"
#include "../Timeline/AutomationLanes/AutomationLanesModulatorHost.h"
#include "../Timeline/TimelinePanel/TimelinePanelTestFixture.h"
#include "BottomDockActiveTabResetGuard.h"
#include "UI/Layout/KeyGlide/RestoredColourGlide.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"
#include "UI/Timeline/TimelineClipLaneArea/ClipColourGlide.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TrackColour.h"
#include "UI/Timeline/TrackColourPicker.h"
#include "UndoGlideSurfaces.h"

namespace undo_glide_test {

namespace {

using synth::ui::RestoredColourGlide;

/** A colour whose red is far from `was`'s. */
juce::Colour farFrom(juce::Colour was) { return juce::Colour::fromRGB(was.getRed() < 128 ? 255 : 0, 90, 200); }

/** Every fixture lets glides run as if their owners were on screen for as long as it lives. */
struct OffScreenGlides {
    OffScreenGlides() { RestoredColourGlide::setAnimateOffScreenForTest(true); }
    ~OffScreenGlides() { RestoredColourGlide::setAnimateOffScreenForTest(false); }
};

/** The colour picker's commit for one track (the function the swatch click runs), then one undo step. */
void commitTrackColour(synth::TimelineDoc& doc, AppUndoManager& undo, synth::TrackId track, juce::Colour to) {
    synth::ui::TrackColourPickerContext context;
    context.doc = &doc;
    context.targets = {track};
    context.performEdit = [&](const std::function<void()>& edit) { undo.recordTimelineChange(doc, edit); };
    auto picker = synth::ui::buildTrackColourPicker(context, track);
    picker->setCurrentColourForTest(to);
    picker->commitForTest();
}

enum class Shows { header, clips, mixer };

/** A MIDI track with a clip, in the app's timeline panel and mixer; the track's colour committed through the picker. */
class TrackColourSurface : public Surface {
public:
    explicit TrackColourSurface(Shows shows)
        : shows_(shows) {
        mc_.setSize(1400, 900);
        if (shows_ == Shows::mixer) {
            mc_.getAudioEngine().suspendDeviceCallback();
            mc_.getBottomDock().getMixerPanel().setVisible(true); // a hidden mixer defers its rebuilds
        }
        mc_.newPatchForTest();
        if (shows_ == Shows::mixer)
            mc_.simulateAddAudioTrackClick(); // a channel strip with its own column
        else
            mc_.simulateAddMidiTrackClick();
        panel().setSize(1400, 700);
        track_ = doc().getTracks().front().id;
        if (shows_ == Shows::clips)
            clip_ = doc().addClip(track_, 0.0, 4.0, "Clip");
        if (shows_ == Shows::mixer) {
            for (int i = 0; i < 3; ++i)
                synth::test::flushMessageQueue();
            auto& mixer = mc_.getBottomDock().getMixerPanel();
            mixer.setSize(1400, 300);
            mixer.rebuild();
            mixer.resized();
        }
    }

    void move() override {
        commitTrackColour(doc(), mc_.getUndoManager(), track_, farFrom(resting()));
        settle();
    }
    bool undo() override { return mc_.getUndoManager().undo(); }
    bool redo() override { return mc_.getUndoManager().redo(); }
    double drawn() const override { return drawnColour().getRed(); }
    bool gliding() const override { return glide().isGliding(); }
    void stepTo(double elapsedMs) override { glide().stepForTest(elapsedMs); }
    juce::Component* paintedComponent() override {
        switch (shows_) {
        case Shows::header:
            return header();
        case Shows::clips:
            return &panel().getClipLaneArea();
        case Shows::mixer:
            return column() != nullptr ? &column()->getHeaderForTest() : nullptr;
        }
        return nullptr;
    }

private:
    synth::TimelineDoc& doc() { return mc_.getTimelineDoc(); }
    const synth::Track& track() const { return *mc_.getTimelineDoc().getTrack(track_); }
    synth::ui::TimelinePanelComponent& panel() { return mc_.getTimelinePanel(); }
    synth::ui::TimelineTrackHeaderComponent* header() const {
        for (int i = 0; i < mc_.getTimelinePanel().getTrackHeaderCount(); ++i)
            if (mc_.getTimelinePanel().getTrackHeaderAt(i)->getTrackId() == track_)
                return mc_.getTimelinePanel().getTrackHeaderAt(i);
        return nullptr;
    }
    synth::ui::MixerColumnComponent* column() const {
        return mc_.getBottomDock().getMixerPanel().getStripColumnForTest(0);
    }
    juce::Colour resting() const { return synth::ui::resolveTrackColour(track().colourArgb, 0, false); }

    RestoredColourGlide& glide() const {
        switch (shows_) {
        case Shows::header:
            return header()->colourGlide();
        case Shows::clips:
            return mc_.getTimelinePanel().getClipLaneArea().getColourGlideForTest().glide;
        default:
            return column()->headerColourGlide();
        }
    }
    juce::Colour drawnColour() const {
        switch (shows_) {
        case Shows::header:
            return header()->getDrawnColour();
        case Shows::clips:
            return glide().drawn(clip_.value, resting());
        default:
            return column()->getDrawnHeaderColour();
        }
    }
    void settle() { glide().stepForTest(1000.0); }

    OffScreenGlides offScreen_;
    BottomDockActiveTabResetGuardMDT resetGuard_;
    mutable MainComponent mc_{std::make_unique<MockProviderTL>()};
    Shows shows_;
    synth::TrackId track_;
    synth::ClipId clip_;
};

enum class Lane { header, modulatorRow };

/** An automation lane (with an LFO routing) under a Bass track: its header's stripe and the modulator row's tag. */
class LaneColourSurface : public Surface {
public:
    explicit LaneColourSurface(Lane lane)
        : lane_(lane) {
        h_.host.modulators.push_back(modulator_rows_test::lfoInfo("1"));
        bass_ = doc().addTrack(synth::TrackKind::Midi, "Bass");
        cutoff_ = h_.f.addLane(bass_, "cutoff");
        h_.f.panel.setTrackAutomationExpanded(bass_, true);
        glide().stepForTest(1000.0);
    }

    void move() override {
        commitTrackColour(doc(), h_.f.undo, bass_, farFrom(resting()));
        glide().stepForTest(1000.0);
    }
    bool undo() override { return h_.f.undo.undo(); }
    bool redo() override { return h_.f.undo.redo(); }
    double drawn() const override {
        const auto rest = resting();
        return (lane_ == Lane::header ? header()->colourGlide() : row()->colourGlide()).drawn(0, rest).getRed();
    }
    bool gliding() const override { return glide().isGliding(); }
    void stepTo(double elapsedMs) override { glide().stepForTest(elapsedMs); }
    juce::Component* paintedComponent() override {
        return lane_ == Lane::header ? static_cast<juce::Component*>(header()) : row();
    }

private:
    synth::TimelineDoc& doc() { return h_.f.doc; }
    juce::Colour resting() const {
        const auto* t = h_.f.doc.getTrack(bass_);
        return synth::ui::resolveTrackColour(t->colourArgb, 0, false);
    }
    synth::ui::AutomationLaneHeaderComponent* header() const { return h_.f.panel.laneHeaderForTest(cutoff_); }
    synth::ui::ModulatorRow* row() const { return h_.f.panel.getAutomationLanes().modulatorRowFor(cutoff_, 0); }
    RestoredColourGlide& glide() const {
        return lane_ == Lane::header ? header()->colourGlide() : row()->colourGlide();
    }

    OffScreenGlides offScreen_;
    mutable modulator_rows_test::HostedLanes h_;
    Lane lane_;
    synth::TrackId bass_;
    synth::LaneId cutoff_;
};

} // namespace

void appendTrackColourEntries(std::vector<Entry>& list) {
    list.push_back(
        {"Track colour (timeline header)", false, [] { return std::make_unique<TrackColourSurface>(Shows::header); }});
    list.push_back({"Track colour (clips)", false, [] { return std::make_unique<TrackColourSurface>(Shows::clips); }});
    list.push_back(
        {"Track colour (mixer strip)", false, [] { return std::make_unique<TrackColourSurface>(Shows::mixer); }});
    list.push_back(
        {"Automation lane colour (header)", false, [] { return std::make_unique<LaneColourSurface>(Lane::header); }});
    list.push_back(
        {"Modulator row colour", false, [] { return std::make_unique<LaneColourSurface>(Lane::modulatorRow); }});
}

} // namespace undo_glide_test
