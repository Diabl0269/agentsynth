// TimelineTrackHeaderMonitorTests.cpp: the audio track header's Monitor button (docs/timeline/tracks.md#monitoring)
// -- which tracks show it, what it shows for each mode, the click and the row's I key as one undoable edit, and the
// name, value and tooltip a screen reader or a hover gets.
#include "../Accessibility/AccessibilityAudit.h"
#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>

namespace {

using synth::MonitorMode;
using synth::TrackKind;
using synth::ui::TimelineTrackHeaderComponent;
using synth::ui::TrackHeaderHost;

class MonitorStubHost : public TrackHeaderHost {
public:
    MonitorStubHost(synth::TimelineDoc& doc, AppUndoManager& undo)
        : doc_(doc)
        , undo_(undo) {}
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(synth::TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        undo_.recordTimelineChange(doc_, mutation);
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }

private:
    synth::TimelineDoc& doc_;
    AppUndoManager& undo_;
};

struct MonitorHeader {
    explicit MonitorHeader(TrackKind kind = TrackKind::Audio) {
        id = doc.addTrack(kind, "Vox");
        header = std::make_unique<TimelineTrackHeaderComponent>(doc, id, &host);
        header->setSize(220, TimelineTrackHeaderComponent::kRowHeight);
        header->refreshFromDoc();
    }
    const synth::Track* track() const { return doc.getTrack(id); }
    void set(MonitorMode mode, bool armed) {
        doc.setTrackMonitorMode(id, mode);
        doc.setTrackArmed(id, armed);
        header->refreshFromDoc();
    }

    synth::TimelineDoc doc;
    AppUndoManager undo;
    MonitorStubHost host{doc, undo};
    synth::TrackId id;
    std::unique_ptr<TimelineTrackHeaderComponent> header;
};

juce::KeyPress plain(juce::juce_wchar c) { return juce::KeyPress(c, juce::ModifierKeys::noModifiers, c); }

} // namespace

TEST(TimelineTrackHeaderMonitorTest, OnlyAudioTracksShowTheButtonAndMsrLineUpOnEveryRow) {
    MonitorHeader audio;
    MonitorHeader midi(TrackKind::Midi);
    EXPECT_TRUE(audio.header->getMonitorButton().isVisible());
    EXPECT_FALSE(midi.header->getMonitorButton().isVisible());
    EXPECT_EQ(audio.header->getArmButton().getX(), midi.header->getArmButton().getX())
        << "the Monitor slot is kept on every row so M, S and R line up";
    EXPECT_GT(audio.header->getMonitorButton().getX(), audio.header->getArmButton().getRight())
        << "the speaker sits right of R";
    EXPECT_FALSE(midi.header->keyPressed(plain('i'))) << "a MIDI track leaves I to bubble";
}

TEST(TimelineTrackHeaderMonitorTest, TheButtonShowsEachModeAndTheScreenReaderValueFollows) {
    MonitorHeader f;
    auto& button = f.header->getMonitorButton();
    EXPECT_EQ(button.getLitLevel(), 0) << "Auto, not armed: nothing to hear";
    EXPECT_EQ(button.getModeText(), "Auto");

    f.set(MonitorMode::Auto, true);
    EXPECT_EQ(button.getLitLevel(), 1);
    EXPECT_EQ(button.getModeText(), "Auto, hearing the input");

    f.set(MonitorMode::On, false);
    EXPECT_EQ(button.getLitLevel(), 2);
    EXPECT_EQ(button.getModeText(), "On");
    EXPECT_TRUE(button.getTooltip().contains("Monitor: On"));

    f.set(MonitorMode::Off, true);
    EXPECT_EQ(button.getLitLevel(), 0);
    EXPECT_EQ(button.getModeText(), "Off");

    EXPECT_EQ(button.getTitle(), "Monitor");
    EXPECT_FALSE(button.getWantsKeyboardFocus()) << "like M/S/R, the row's key does it";
    auto handler = button.createAccessibilityHandler();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::button);
    ASSERT_NE(handler->getValueInterface(), nullptr);
    EXPECT_EQ(handler->getValueInterface()->getCurrentValueAsString(), "Off");

    // The name label is left out: JUCE's label handler speaks the label's text as its name, which the audit
    // cannot see headlessly.
    for (const auto& gap : synth::test::auditAccessibility(*f.header))
        if (!gap.path.contains("trackNameLabel"))
            ADD_FAILURE() << (gap.kind == synth::test::Gap::Kind::MissingName ? "missing name " : "missing tooltip ")
                          << gap.path;
}

TEST(TimelineTrackHeaderMonitorTest, ClickAndTheIKeySwitchOnAndAutoAsOneUndoStepEach) {
    MonitorHeader f;
    f.header->getMonitorButton().onClick();
    EXPECT_EQ(f.track()->monitorMode, MonitorMode::On);
    EXPECT_TRUE(f.header->keyPressed(plain('i')));
    EXPECT_EQ(f.track()->monitorMode, MonitorMode::Auto);

    f.set(MonitorMode::Off, false);
    EXPECT_TRUE(f.header->keyPressed(plain('i')));
    EXPECT_EQ(f.track()->monitorMode, MonitorMode::On) << "from Off, a click turns monitoring on";

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.track()->monitorMode, MonitorMode::Off) << "the set() above is not on the undo stack";
    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.track()->monitorMode, MonitorMode::On);
    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.track()->monitorMode, MonitorMode::Auto);
    EXPECT_FALSE(f.undo.canUndo());
}
