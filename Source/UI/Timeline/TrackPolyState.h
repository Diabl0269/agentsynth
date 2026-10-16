#pragma once

namespace synth::ui {

/** What a track's voice graph (the modules its source feeds that can be poly) is set to; drives the track header's
 *  Mono/Poly switch. `none` hides the switch: a track with nothing poly-capable has nothing to switch. */
enum class TrackPolyState { none, mono, poly, mixed };

} // namespace synth::ui
