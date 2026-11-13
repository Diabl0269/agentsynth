#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::ui {

// The amount lane: how much a modulator moves its parameter, over the song. It is an ordinary automation
// lane on the routing's hidden Attenuverter node (`amount`, -1..1, below 0 = the modulator inverted), so the
// engine needs nothing new: the automation applier plays it like any other lane. A routing with no amount
// lane plays the Attenuverter's own knob value. The lane is only ever created by an edit that also gives it
// points: an EMPTY lane would make the applier write its range default (0) every block while playing.
// No GUI in here. docs/timeline/automation.md#amount-lane

/** The Attenuverter parameter an amount lane drives. */
inline const juce::String kAmountParamId = "amount";

/** An amount lane's range: -1..1, default 0. */
synth::AutomationLane::RangeSnapshot amountLaneRange() noexcept;

/** The amount lane of the routing whose Attenuverter is `attenuverterUuid`, wherever it sits, or nullptr. */
const synth::AutomationLane* amountLaneFor(const synth::TimelineDoc& doc, const juce::String& attenuverterUuid);

/**
 * Makes `points` the routing's amount lane: creates it on `track` when there is none, replaces every point,
 * and removes the lane when `points` is empty (so erasing the last point goes back to the knob value). The
 * caller wraps this in ONE undo step. False when nothing changed.
 */
bool writeAmountLane(synth::TimelineDoc& doc, synth::TrackId track, const juce::String& attenuverterUuid,
                     const std::vector<synth::AutomationLane::Breakpoint>& points);

/** "+72%", "-30%" (a real minus sign), "0%": an amount (-1..1) as the row and the screen reader say it. */
juce::String amountText(double amount);

} // namespace synth::ui
