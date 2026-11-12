// AutomationLanesNameWidthTests.cpp -- every lane header that holds a LaneNameButton (an automation lane's header, a
// modulator row) lays the button out at the width its name needs, and keeps doing so when the name changes under it.
// Against a stub host, headless.

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AutomationLanesModulatorHost.h"

using namespace modulator_rows_test;
using synth::TrackKind;
using synth::ui::LaneNameButton;

namespace {

// Longer than the names the scene starts with, short enough for the narrowest header column the scene lays out.
const juce::String kLongName = "Wide LFO 2";

struct NameWidthScene {
    HostedLanes h;
    synth::TrackId bass;
    synth::LaneId cutoff;

    NameWidthScene() {
        h.host.modulators = {lfoInfo("1")};
        bass = h.f.doc.addTrack(TrackKind::Midi, "Bass");
        cutoff = h.f.addLane(bass, "cutoff");
        h.f.panel.setTrackAutomationExpanded(bass, true);
    }

    // Every header of the scene that owns a name button, labelled for the failure message.
    std::vector<std::pair<juce::String, LaneNameButton*>> nameButtons() {
        std::vector<std::pair<juce::String, LaneNameButton*>> out;
        if (auto* header = h.f.panel.laneHeaderForTest(cutoff))
            out.emplace_back("AutomationLaneHeaderComponent", &header->getNameButton());
        if (auto* row = h.f.panel.modulatorRowForTest(cutoff, 0))
            out.emplace_back("ModulatorRow", &row->getNameButton());
        return out;
    }
};

} // namespace

TEST(AutomationLanesNameWidthTest, EveryHeaderThatOwnsANameButtonIsFoundHere) {
    NameWidthScene s;
    EXPECT_EQ(s.nameButtons().size(), 2u) << "a new owner of a LaneNameButton joins nameButtons() and so this guard";
}

// Regression test for FRO878: a header's name button was sized from its name only when something re-laid the header
// out, so a name set after the layout stayed cut to "L..".
TEST(AutomationLanesNameWidthTest, ALongerNameWidensTheButtonWithoutAnyResizeOfTheHeader) {
    NameWidthScene s;
    for (auto& [owner, button] : s.nameButtons()) {
        const int before = button->getWidth();
        button->setName(kLongName);
        ASSERT_GT(button->getPreferredWidth(), before) << owner;
        EXPECT_GE(button->getWidth(), button->getPreferredWidth())
            << owner << ": the button must fit its name as soon as the name changes";
    }
}

TEST(AutomationLanesNameWidthTest, ARenamedSourceUnderTheSameKeyKeepsTheModulatorRowsNameWhole) {
    NameWidthScene s;
    s.h.host.modulators[0].sourceTitle = kLongName;
    s.h.f.panel.refreshModulators();
    auto* row = s.h.f.panel.modulatorRowForTest(s.cutoff, 0);
    ASSERT_NE(row, nullptr);
    EXPECT_GE(row->getNameButton().getWidth(), row->getNameButton().getPreferredWidth());
}
