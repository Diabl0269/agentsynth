#pragma once

// LoadRevealTimeline.h -- the pure timing of a project coming to life (docs/layout/animation.md#project-load-reveal):
// when each group of cards pops in, and when each cable draws out. No components: LoadRevealAnimator applies it.

#include <utility>
#include <vector>

namespace synth::ui::load_reveal {

constexpr double kPopMs = 160.0;            ///< A card grows in (growScale, the 8% bounce).
constexpr double kCableMs = 120.0;          ///< A cable draws out from its source once both ends have appeared.
constexpr double kWaveSpanMs = 120.0;       ///< The last wave level starts no later than this after the first.
constexpr double kMaxLevelStepMs = 40.0;    ///< Between two wave levels when there are few.
constexpr double kReducedFadeMs = 80.0;     ///< Reduce Motion: the canvas fades in as a whole.
constexpr double kStageLineDelayMs = 400.0; ///< A slow load says what it is waiting for after this long.

enum class Motion { full, reduced, off };

/** Times are ms since the reveal started. A group is a wave unit: a card, a collapsed macro card, or an open macro
 *  with every card inside it. Under reduced or off motion nothing pops or draws: a group is simply there from its
 *  start on, and a cable as soon as both ends are. */
class Timeline {
public:
    /** `levels[g]` is group g's wave level; a `pending` group waits for markReady. */
    void start(Motion motion, const std::vector<int>& levels, const std::vector<bool>& pending);
    /** A pending group's assets are in at `nowMs`: it pops then, or at its wave slot if that is later. */
    void markReady(int group, double nowMs);

    int groupCount() const noexcept { return (int)start_.size(); }
    bool isScheduled(int group) const noexcept;
    bool allScheduled() const noexcept;
    double levelStepMs() const noexcept { return step_; }
    double popMs() const noexcept { return motion_ == Motion::full ? kPopMs : 0.0; }
    double cableMs() const noexcept { return motion_ == Motion::full ? kCableMs : 0.0; }

    /** 0 before the group starts, 1 once it has fully popped in. */
    float popProgress(int group, double nowMs) const noexcept;
    /** When the group has fully appeared; a huge value while it is pending. A group index < 0 (no group: a card the
     *  reveal does not know) has always appeared. */
    double appearedMs(int group) const noexcept;
    /** 0 until both ends have appeared, then eased out to 1 over kCableMs; 1 when neither end is a group. */
    float cableProgress(int sourceGroup, int destGroup, double nowMs) const noexcept;
    /** When everything scheduled lands, the cables included; a huge value while any group is pending. */
    double endMs(const std::vector<std::pair<int, int>>& cables) const noexcept;

private:
    Motion motion_ = Motion::full;
    std::vector<double> slot_;  // the wave slot of each group
    std::vector<double> start_; // when it starts popping; negative while pending
    double step_ = 0.0;
};

} // namespace synth::ui::load_reveal
