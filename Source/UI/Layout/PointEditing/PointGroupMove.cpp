#include "PointGroupMove.h"
#include <algorithm>
#include <limits>

namespace synth::ui {

PointRoom roomWithin(double position, double lo, double hi) { return {lo - position, hi - position}; }

// The nearest non-moving dot on each side bounds a moving one; a dot that moves too travels with it, so it is no
// limit.
std::vector<PointRoom> roomBetweenNeighbours(const std::vector<double>& positions, const std::vector<bool>& moving,
                                             double lo, double hi) {
    std::vector<PointRoom> rooms;
    const int count = (int)positions.size();
    for (int i = 0; i < count; ++i) {
        if (!moving[(size_t)i])
            continue;
        double lower = lo;
        for (int j = i - 1; j >= 0; --j)
            if (!moving[(size_t)j]) {
                lower = positions[(size_t)j];
                break;
            }
        double upper = hi;
        for (int j = i + 1; j < count; ++j)
            if (!moving[(size_t)j]) {
                upper = positions[(size_t)j];
                break;
            }
        rooms.push_back(roomWithin(positions[(size_t)i], lower, upper));
    }
    return rooms;
}

// The limits are widened to include zero: a dot that already sits past a limit (a range that shrank under it) may
// stay put, but the block never jumps backwards to fix it.
double clampGroupDelta(const std::vector<PointRoom>& rooms, double delta) {
    double lo = -std::numeric_limits<double>::infinity();
    double hi = std::numeric_limits<double>::infinity();
    for (const auto& room : rooms) {
        lo = std::max(lo, room.below);
        hi = std::min(hi, room.above);
    }
    return std::clamp(delta, std::min(lo, 0.0), std::max(hi, 0.0));
}

} // namespace synth::ui
