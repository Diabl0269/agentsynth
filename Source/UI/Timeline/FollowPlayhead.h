#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

// How "follow playhead" keeps the playing cursor on screen, as pure functions so the arrangement view and the
// piano roll share one rule and the rule tests without a component.
namespace synth::ui {

// Page jumps a screenful when the cursor leaves the view; Smooth scrolls continuously so the cursor stays pinned at
// one point of the visible width.
enum class FollowMode { Page, Smooth };

inline constexpr int kMinFollowPointPercent = 10;
inline constexpr int kMaxFollowPointPercent = 90;
inline constexpr int kDefaultFollowPointPercent = 50;
// Where Page mode lands a cursor that left the view, as a fraction of the visible width.
inline constexpr double kPageLandingFraction = 0.1;

inline int clampFollowPointPercent(int percent) noexcept {
    return std::clamp(percent, kMinFollowPointPercent, kMaxFollowPointPercent);
}

// The persisted spelling; an unknown or missing value is Page, the behaviour before modes existed.
inline constexpr const char* followModeKey(FollowMode mode) noexcept {
    return mode == FollowMode::Smooth ? "smooth" : "page";
}
inline constexpr FollowMode followModeFromKey(std::string_view key) noexcept {
    return key == "smooth" ? FollowMode::Smooth : FollowMode::Page;
}
inline constexpr FollowMode otherFollowMode(FollowMode mode) noexcept {
    return mode == FollowMode::Page ? FollowMode::Smooth : FollowMode::Page;
}
inline const char* followModeName(FollowMode mode) noexcept { return mode == FollowMode::Smooth ? "Smooth" : "Page"; }

// Settings keys: the choice rides next to the on/off flag ("timelineFollowPlayhead"); a missing key is Page / 50%.
inline constexpr const char* kFollowModeKey = "timelineFollowMode";
inline constexpr const char* kFollowPointPercentKey = "timelineFollowPointPercent";

struct FollowPlayheadState {
    bool enabled = false;
    FollowMode mode = FollowMode::Page;
    int pointPercent = kDefaultFollowPointPercent;
};

// The first visible beat that follow wants for a cursor at `playheadBeat`, given the current one. Page keeps the
// origin while the cursor is inside [origin, origin + visibleBeats] and otherwise lands the cursor a tenth in; Smooth
// always pins the cursor at `pointPercent` of the width (the origin stops at 0, so near the start the cursor travels
// to the pin first). Never negative.
inline double followViewOrigin(FollowMode mode, int pointPercent, double playheadBeat, double currentOrigin,
                               double visibleBeats) noexcept {
    if (!std::isfinite(playheadBeat) || !std::isfinite(currentOrigin) || !(visibleBeats > 0.0))
        return std::max(0.0, std::isfinite(currentOrigin) ? currentOrigin : 0.0);
    if (mode == FollowMode::Smooth)
        return std::max(0.0, playheadBeat - clampFollowPointPercent(pointPercent) / 100.0 * visibleBeats);
    if (playheadBeat >= currentOrigin && playheadBeat <= currentOrigin + visibleBeats)
        return currentOrigin;
    return std::max(0.0, playheadBeat - kPageLandingFraction * visibleBeats);
}

} // namespace synth::ui
