// TimelineTrackHeaderInternal.h
//
// Private per-TrackKind badge helpers shared by the TimelineTrackHeaderComponent*.cpp units (the
// test accessors in TimelineTrackHeaderComponent.cpp and the badge painting in
// TimelineTrackHeaderComponentPaint.cpp). Not registered as a CMake source -- header only.
// `inline` so each unit resolves to one definition.
#pragma once

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include "Timeline/TimelineDoc/TimelineDoc.h"

#include <juce_core/juce_core.h>

namespace synth::ui::detail {

// Fixed per-TrackKind label. Never edited, never doc-driven beyond the kind itself. Kept as the
// fallback badge content for a headless build (no AppLookAndFeel) or one with no asset library —
// see getKindBadgeIcon()/paint().
inline juce::String kindBadgeText(synth::TrackKind kind) {
    switch (kind) {
    case synth::TrackKind::Midi:
        return "MIDI";
    case synth::TrackKind::Audio:
        return "AUD";
    case synth::TrackKind::Automation:
        return "Auto";
    }
    return {};
}

// Fixed per-TrackKind glyph. Automation has no dedicated colour-swatch analogue in the icon set
// beyond TrackAutomation itself, so the mapping is 1:1 with kindBadgeText's switch.
inline synth::theme::Icon kindBadgeIcon(synth::TrackKind kind) {
    switch (kind) {
    case synth::TrackKind::Midi:
        return synth::theme::Icon::TrackMidi;
    case synth::TrackKind::Audio:
        return synth::theme::Icon::TrackAudio;
    case synth::TrackKind::Automation:
        return synth::theme::Icon::TrackAutomation;
    }
    return synth::theme::Icon::TrackMidi;
}

} // namespace synth::ui::detail
