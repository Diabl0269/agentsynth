// ControllerDetectTests.cpp -- FRO134 (docs/control/midi-remote-ui.md#detect-mode): the headless
// half of Detect -- which events may create a control, what it is called/typed, where it lands.
#include "MidiRemote/ControllerDetect.h"

#include <gtest/gtest.h>

using namespace synth;
using namespace synth::midi;

namespace {

RemoteEvent event(MessageType type, int channel, int number, RemoteEventKind kind = RemoteEventKind::absolute) {
    RemoteEvent e;
    e.specType = static_cast<std::uint8_t>(type);
    e.specChannel = static_cast<std::uint8_t>(channel);
    e.specNumber = static_cast<std::uint8_t>(number);
    e.kind = kind;
    return e;
}

} // namespace

TEST(ControllerDetectTests, CcBecomesAKnobNamedByItsNumber) {
    const auto control = makeDetectedControl(event(MessageType::cc, 1, 21), {});
    EXPECT_EQ(control.kind, ControlKind::knob);
    EXPECT_EQ(control.name, "CC 21");
    EXPECT_EQ(control.message.type, MessageType::cc);
    EXPECT_EQ(control.message.channel, 1);
    EXPECT_EQ(control.message.number, 21);
    EXPECT_FALSE(control.id.isEmpty());
}

TEST(ControllerDetectTests, NoteBecomesAPadNamedByItsPitch) {
    const auto control = makeDetectedControl(event(MessageType::note, 10, 60, RemoteEventKind::buttonPress), {});
    EXPECT_EQ(control.kind, ControlKind::pad);
    EXPECT_EQ(control.name, "C3");
}

TEST(ControllerDetectTests, PitchBendBecomesAWheel) {
    const auto control = makeDetectedControl(event(MessageType::pitchBend, 1, 0), {});
    EXPECT_EQ(control.kind, ControlKind::wheel);
    EXPECT_EQ(control.name, "Pitch Bend");
}

TEST(ControllerDetectTests, NewControlsTakeTheNextFreeGridCellInTouchOrder) {
    std::vector<Control> controls;
    for (int i = 0; i < 10; ++i) {
        controls.push_back(makeDetectedControl(event(MessageType::cc, 1, 21 + i), controls));
    }
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(controls[static_cast<std::size_t>(i)].layout.col, i);
        EXPECT_EQ(controls[static_cast<std::size_t>(i)].layout.row, 0);
    }
    EXPECT_EQ(controls[8].layout.col, 0);
    EXPECT_EQ(controls[8].layout.row, 1);
    EXPECT_EQ(controls[9].layout.col, 1);
}

TEST(ControllerDetectTests, FreedCellIsReusedBeforeTheGridGrows) {
    std::vector<Control> controls;
    for (int i = 0; i < 3; ++i)
        controls.push_back(makeDetectedControl(event(MessageType::cc, 1, 21 + i), controls));
    controls.erase(controls.begin() + 1); // the user deleted the middle one
    const auto next = makeDetectedControl(event(MessageType::cc, 1, 30), controls);
    EXPECT_EQ(next.layout.col, 1);
    EXPECT_EQ(next.layout.row, 0);
}

TEST(ControllerDetectTests, OnlyPressesAndValuesMayCreateAControl) {
    EXPECT_TRUE(isDetectCandidate(event(MessageType::cc, 1, 21)));
    EXPECT_TRUE(isDetectCandidate(event(MessageType::note, 1, 60, RemoteEventKind::buttonPress)));
    EXPECT_FALSE(isDetectCandidate(event(MessageType::note, 1, 60, RemoteEventKind::buttonRelease)));
    EXPECT_FALSE(isDetectCandidate(event(MessageType::cc, 1, 21, RemoteEventKind::learnCandidate)));
    EXPECT_FALSE(isDetectCandidate(event(MessageType::programChange, 1, 5, RemoteEventKind::buttonPress)));
}

TEST(ControllerDetectTests, FindControlHonoursChannelZeroAsAny) {
    std::vector<Control> controls;
    Control any;
    any.id = "a";
    any.message = {MessageType::cc, 0, 21};
    Control ch2;
    ch2.id = "b";
    ch2.message = {MessageType::cc, 2, 22};
    controls = {any, ch2};

    const auto* onAnyChannel = findControlForEvent(controls, event(MessageType::cc, 7, 21));
    ASSERT_NE(onAnyChannel, nullptr);
    EXPECT_EQ(onAnyChannel->id, "a");
    EXPECT_NE(findControlForEvent(controls, event(MessageType::cc, 2, 22)), nullptr);
    EXPECT_EQ(findControlForEvent(controls, event(MessageType::cc, 3, 22)), nullptr);
    EXPECT_EQ(findControlForEvent(controls, event(MessageType::note, 2, 22)), nullptr);
}
