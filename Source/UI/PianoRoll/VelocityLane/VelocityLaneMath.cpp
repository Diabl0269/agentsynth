// VelocityLaneMath — the velocity strip's pure maths (y <-> velocity, stick picking, pen/ramp line
// values, humanize). No Component and no theme: PianoRollVelocityLane feeds it stick positions in
// its own pixel space and applies what comes back.

#include "VelocityLaneMath.h"

#include <cmath>
#include <limits>

namespace synth::ui::velocitylane {

int clampVelocity(int velocity) noexcept { return juce::jlimit(kMinVelocity, kMaxVelocity, velocity); }

// Linear, velocity 127 at `top` and velocity 1 at `bottom`: the strip's full height is the full
// MIDI range, so the scale labels in the gutter (127 / 64 / 1) read straight off it.
float yForVelocity(int velocity, float top, float bottom) noexcept {
    const float t = (float)(clampVelocity(velocity) - kMinVelocity) / (float)(kMaxVelocity - kMinVelocity);
    return bottom - t * (bottom - top);
}

// The exact inverse of yForVelocity, rounded to the nearest whole velocity and clamped — a pointer
// dragged above the strip's top pins at 127 rather than wrapping or going out of range.
int velocityForY(float y, float top, float bottom) noexcept {
    if (bottom <= top)
        return kMaxVelocity;
    const float t = (bottom - y) / (bottom - top);
    return clampVelocity((int)std::lround(kMinVelocity + t * (float)(kMaxVelocity - kMinVelocity)));
}

// Nearest stick by horizontal distance, within the tolerance. Sticks sharing an x (a chord) tie on
// that, so the tie goes to the head nearest the pointer's y — otherwise a chord's upper notes would
// be unreachable behind whichever note happens to come first.
std::optional<size_t> pickStick(const std::vector<StickPoint>& sticks, juce::Point<float> pos, int tolerancePx) {
    std::optional<size_t> best;
    float bestDx = std::numeric_limits<float>::max();
    float bestDy = std::numeric_limits<float>::max();
    for (size_t i = 0; i < sticks.size(); ++i) {
        const float dx = std::abs((float)sticks[i].x - pos.x);
        if (dx > (float)tolerancePx)
            continue;
        const float dy = std::abs(sticks[i].headY - pos.y);
        const bool sameColumn = std::abs(dx - bestDx) < 0.5f;
        if (!best || (sameColumn ? dy < bestDy : dx < bestDx)) {
            best = i;
            bestDx = dx;
            bestDy = dy;
        }
    }
    return best;
}

// Serves both the pen (one call per mouse segment, so a fast stroke that jumps 100 px between two
// events still lands on every stick in between) and the ramp (one call from the anchor to the
// pointer). The span is inclusive in whole pixels, so a zero-length segment — the press itself —
// still sets a stick sitting exactly under it.
std::vector<std::pair<synth::NoteId, int>> lineVelocities(const std::vector<StickPoint>& sticks,
                                                          juce::Point<float> from, juce::Point<float> to, float top,
                                                          float bottom) {
    std::vector<std::pair<synth::NoteId, int>> out;
    const long lo = std::lround(std::min(from.x, to.x));
    const long hi = std::lround(std::max(from.x, to.x));
    const float spanX = to.x - from.x;
    for (const auto& stick : sticks) {
        if ((long)stick.x < lo || (long)stick.x > hi)
            continue;
        float y = to.y;
        if (std::abs(spanX) > 1.0e-3f) {
            const float t = juce::jlimit(0.0f, 1.0f, ((float)stick.x - from.x) / spanX);
            y = from.y + t * (to.y - from.y);
        }
        out.emplace_back(stick.id, velocityForY(y, top, bottom));
    }
    return out;
}

// The random source is the caller's, so a test seeds it and gets the same offsets every run.
int humanizedVelocity(int velocity, int range, juce::Random& random) {
    const int r = std::max(0, range);
    return clampVelocity(velocity + random.nextInt(juce::Range<int>(-r, r + 1)));
}

} // namespace synth::ui::velocitylane
