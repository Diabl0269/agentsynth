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
    e.specNumber = static_cast<std::uint16_t>(number); // 16 bits: an nrpn address is 14-bit
    e.kind = kind;
    return e;
}

RemoteEvent ccAt(int channel, int number, int timeMs) {
    auto e = event(MessageType::cc, channel, number);
    e.timeMs = static_cast<std::uint16_t>(timeMs);
    return e;
}

Control controlOn(const juce::String& id, MessageType type, int channel, int number, Encoding encoding) {
    Control c;
    c.id = id;
    c.message = {type, channel, number};
    c.encoding = encoding;
    return c;
}

/** `previous` as Detect would have recorded it right after adding `control` from `first`. */
DetectedCc lastDetected(const Control& control, const RemoteEvent& first) {
    DetectedCc d;
    d.valid = true;
    d.timeMs = first.timeMs;
    d.channel = first.specChannel;
    d.number = first.specNumber;
    d.controlId = control.id;
    return d;
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

// -- 14-bit pairs and NRPN (FRO140) ------------------------------------------------------------------

TEST(ControllerDetectTests, NrpnBecomesA14BitKnobNamedByItsAddress) {
    const auto control = makeDetectedControl(event(MessageType::nrpn, 1, 300), {});
    EXPECT_EQ(control.kind, ControlKind::knob);
    EXPECT_EQ(control.name, "NRPN 300");
    EXPECT_EQ(control.message.type, MessageType::nrpn);
    EXPECT_EQ(control.message.number, 300);
    EXPECT_EQ(control.encoding, Encoding::abs14);
}

TEST(ControllerDetectTests, ControlMatchesEventOwnMessageAndPairedLsbPartner) {
    const auto paired = controlOn("p", MessageType::cc, 1, 21, Encoding::abs14);
    EXPECT_TRUE(controlMatchesEvent(paired, event(MessageType::cc, 1, 21)));
    EXPECT_TRUE(controlMatchesEvent(paired, event(MessageType::cc, 1, 53)));
    EXPECT_FALSE(controlMatchesEvent(paired, event(MessageType::cc, 2, 53))); // other channel
    EXPECT_FALSE(controlMatchesEvent(paired, event(MessageType::cc, 1, 54)));
    EXPECT_FALSE(controlMatchesEvent(paired, event(MessageType::note, 1, 53)));

    const auto lsbFirst = controlOn("l", MessageType::cc, 1, 21, Encoding::abs14LsbFirst);
    EXPECT_TRUE(controlMatchesEvent(lsbFirst, event(MessageType::cc, 1, 53)));
}

TEST(ControllerDetectTests, ChannelAnyPairedControlMatchesItsPartnerOnAnyChannel) {
    const auto paired = controlOn("p", MessageType::cc, 0, 21, Encoding::abs14);
    EXPECT_TRUE(controlMatchesEvent(paired, event(MessageType::cc, 9, 53)));
    EXPECT_TRUE(controlMatchesEvent(paired, event(MessageType::cc, 16, 21)));
}

TEST(ControllerDetectTests, UnpairedControlDoesNotOwnTheNPlus32Cc) {
    const auto plain = controlOn("a", MessageType::cc, 1, 21, Encoding::abs7);
    EXPECT_FALSE(controlMatchesEvent(plain, event(MessageType::cc, 1, 53)));
    EXPECT_TRUE(controlMatchesEvent(plain, event(MessageType::cc, 1, 21)));
    EXPECT_FALSE(controlClaimsSpec(plain, {MessageType::cc, 1, 53}));
    EXPECT_TRUE(controlClaimsSpec(plain, {MessageType::cc, 1, 21}));

    const auto relative = controlOn("r", MessageType::cc, 1, 21, Encoding::relTwos);
    EXPECT_FALSE(controlClaimsSpec(relative, {MessageType::cc, 1, 53}));
}

TEST(ControllerDetectTests, PairedControlClaimsItsPartnerSpecOnTheSameChannelOnly) {
    const auto paired = controlOn("p", MessageType::cc, 1, 21, Encoding::abs14);
    EXPECT_TRUE(controlClaimsSpec(paired, {MessageType::cc, 1, 21}));
    EXPECT_TRUE(controlClaimsSpec(paired, {MessageType::cc, 1, 53}));
    EXPECT_FALSE(controlClaimsSpec(paired, {MessageType::cc, 2, 53}));
    EXPECT_FALSE(controlClaimsSpec(paired, {MessageType::cc, 1, 22}));
    EXPECT_FALSE(controlClaimsSpec(paired, {MessageType::note, 1, 53}));
}

TEST(ControllerDetectTests, NrpnControlHasNoCcPartner) {
    // An nrpn address 21 is its own key; CC 53 must not alias to it.
    const auto nrpn = controlOn("n", MessageType::nrpn, 1, 21, Encoding::abs14);
    EXPECT_FALSE(controlMatchesEvent(nrpn, event(MessageType::cc, 1, 53)));
    EXPECT_FALSE(controlClaimsSpec(nrpn, {MessageType::cc, 1, 53}));
}

TEST(ControllerDetectTests, FindControlForEventFindsAPairedControlByItsLsbHalf) {
    const std::vector<Control> controls = {controlOn("p", MessageType::cc, 1, 21, Encoding::abs14)};
    const auto* found = findControlForEvent(controls, event(MessageType::cc, 1, 53));
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, "p");
}

TEST(ControllerDetectTests, FoldMsbThenLsbMakesAnAbs14ControlKeepingItsNumber) {
    const auto first = ccAt(1, 21, 100);
    std::vector<Control> controls = {makeDetectedControl(first, {})};
    const auto previous = lastDetected(controls[0], first);

    EXPECT_TRUE(foldIntoPairedControl(controls, previous, ccAt(1, 53, 103)));
    ASSERT_EQ(controls.size(), 1u);
    EXPECT_EQ(controls[0].encoding, Encoding::abs14);
    EXPECT_EQ(controls[0].message.number, 21);
    EXPECT_EQ(controls[0].name, "CC 21");
}

TEST(ControllerDetectTests, FoldLsbThenMsbMakesAnAbs14LsbFirstControlNumberedByTheMsb) {
    const auto first = ccAt(1, 53, 100);
    std::vector<Control> controls = {makeDetectedControl(first, {})};
    ASSERT_EQ(controls[0].name, "CC 53");
    const auto previous = lastDetected(controls[0], first);

    EXPECT_TRUE(foldIntoPairedControl(controls, previous, ccAt(1, 21, 102)));
    ASSERT_EQ(controls.size(), 1u);
    EXPECT_EQ(controls[0].encoding, Encoding::abs14LsbFirst);
    EXPECT_EQ(controls[0].message.number, 21);
    EXPECT_EQ(controls[0].name, "CC 21"); // renamed with the number
}

TEST(ControllerDetectTests, FoldWindowIsInclusiveAtFiveMilliseconds) {
    const auto first = ccAt(1, 21, 1000);
    std::vector<Control> controls = {makeDetectedControl(first, {})};
    const auto previous = lastDetected(controls[0], first);

    auto tooLate = controls;
    EXPECT_FALSE(foldIntoPairedControl(tooLate, previous, ccAt(1, 53, 1000 + kPairedHalvesWindowMs + 1)));
    EXPECT_EQ(tooLate[0].encoding, Encoding::abs7); // untouched
    EXPECT_TRUE(foldIntoPairedControl(controls, previous, ccAt(1, 53, 1000 + kPairedHalvesWindowMs)));
}

TEST(ControllerDetectTests, FoldUsesModularTimeDifferenceAcrossTheSixteenBitWrap) {
    const auto first = ccAt(1, 21, 65534);
    std::vector<Control> controls = {makeDetectedControl(first, {})};
    const auto previous = lastDetected(controls[0], first);

    EXPECT_TRUE(foldIntoPairedControl(controls, previous, ccAt(1, 53, 2))); // 4 ms later, counter wrapped
    EXPECT_EQ(controls[0].encoding, Encoding::abs14);
}

TEST(ControllerDetectTests, FoldRefusesWhenTheEventTimeIsBeforeThePreviousOne) {
    // Modular difference of an "earlier" time is ~65 s, far outside the window.
    const auto first = ccAt(1, 21, 100);
    std::vector<Control> controls = {makeDetectedControl(first, {})};
    EXPECT_FALSE(foldIntoPairedControl(controls, lastDetected(controls[0], first), ccAt(1, 53, 98)));
}

TEST(ControllerDetectTests, FoldRefusesADifferentChannel) {
    const auto first = ccAt(1, 21, 100);
    std::vector<Control> controls = {makeDetectedControl(first, {})};
    EXPECT_FALSE(foldIntoPairedControl(controls, lastDetected(controls[0], first), ccAt(2, 53, 102)));
    EXPECT_EQ(controls[0].encoding, Encoding::abs7);
}

TEST(ControllerDetectTests, FoldRefusesANumberThatIsNotThePartner) {
    const auto first = ccAt(1, 21, 100);
    std::vector<Control> controls = {makeDetectedControl(first, {})};
    const auto previous = lastDetected(controls[0], first);
    EXPECT_FALSE(foldIntoPairedControl(controls, previous, ccAt(1, 54, 102)));
    EXPECT_FALSE(foldIntoPairedControl(controls, previous, ccAt(1, 22, 102)));
    EXPECT_FALSE(foldIntoPairedControl(controls, previous, ccAt(1, 21, 102))); // the same CC again
    EXPECT_EQ(controls[0].encoding, Encoding::abs7);
}

TEST(ControllerDetectTests, FoldRefusesWithoutAValidPreviousOrANonCcEvent) {
    const auto first = ccAt(1, 21, 100);
    std::vector<Control> controls = {makeDetectedControl(first, {})};
    auto previous = lastDetected(controls[0], first);

    auto notValid = previous;
    notValid.valid = false;
    EXPECT_FALSE(foldIntoPairedControl(controls, notValid, ccAt(1, 53, 101)));

    auto note = event(MessageType::note, 1, 53, RemoteEventKind::buttonPress);
    note.timeMs = 101;
    EXPECT_FALSE(foldIntoPairedControl(controls, previous, note));
    EXPECT_EQ(controls[0].encoding, Encoding::abs7);
}

TEST(ControllerDetectTests, FoldRefusesWhenThePreviousControlIsNotAnAbs7Cc) {
    const auto first = ccAt(1, 21, 100);
    auto relative = makeDetectedControl(first, {});
    relative.encoding = Encoding::relTwos; // the user (or Detect) already classed it as an encoder
    std::vector<Control> controls = {relative};
    EXPECT_FALSE(foldIntoPairedControl(controls, lastDetected(controls[0], first), ccAt(1, 53, 101)));
    EXPECT_EQ(controls[0].encoding, Encoding::relTwos);

    auto alreadyPaired = makeDetectedControl(first, {});
    alreadyPaired.encoding = Encoding::abs14;
    controls = {alreadyPaired};
    EXPECT_FALSE(foldIntoPairedControl(controls, lastDetected(controls[0], first), ccAt(1, 53, 101)));

    controls.clear(); // the control the previous event created was deleted meanwhile
    EXPECT_FALSE(foldIntoPairedControl(controls, lastDetected(alreadyPaired, first), ccAt(1, 53, 101)));
}
