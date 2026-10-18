#pragma once

#include "Modules/Envelope/NoteLength.h"
#include "UI/Timeline/TimelineViewState.h"

namespace synth::ui {

/** The name of a snap value as the Snap box and the status bar write it: "Off", "Bar", "1/1", "1/8T", "1/16D". */
inline juce::String snapValueName(TimelineViewState::Snap value) {
    using Snap = TimelineViewState::Snap;
    if (value == Snap::Off)
        return "Off";
    if (value == Snap::Bar)
        return "Bar";
    const auto kind = TimelineViewState::kindOf(value);
    return noteLengthName({kind == TimelineViewState::SnapKind::dotted    ? NoteKind::dotted
                           : kind == TimelineViewState::SnapKind::triplet ? NoteKind::triplet
                                                                          : NoteKind::straight,
                           1, TimelineViewState::denominatorOf(value)});
}

/** Every snap value's name in enum order, so the item at index i is Snap(i). */
inline juce::StringArray snapValueNames() {
    juce::StringArray names;
    for (int i = (int)TimelineViewState::Snap::Off; i <= (int)TimelineViewState::kLastSnap; ++i)
        names.add(snapValueName((TimelineViewState::Snap)i));
    return names;
}

} // namespace synth::ui
