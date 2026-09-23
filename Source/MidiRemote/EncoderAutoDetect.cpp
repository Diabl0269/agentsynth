// Concern: classifying an encoder's relative encoding from the raw values it sends while turned
// left and right (docs/control/midi-remote-ui.md#inspector-right).

#include "MidiRemote/EncoderAutoDetect.h"

#include "MidiRemote/ControllerDetect.h"

#include <algorithm>

namespace synth::midi {

namespace {

template <typename Pred>
bool allOf(const std::vector<int>& values, Pred pred) {
    return std::all_of(values.begin(), values.end(), pred);
}

bool contains(const std::vector<int>& values, int wanted) {
    return std::find(values.begin(), values.end(), wanted) != values.end();
}

int median(std::vector<int> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

} // namespace

void EncoderAutoDetect::start(const MessageSpec& spec) {
    spec_ = spec;
    left_.clear();
    right_.clear();
    result_.reset();
    phase_ = Phase::turnLeft;
}

void EncoderAutoDetect::cancel() {
    left_.clear();
    right_.clear();
    result_.reset();
    phase_ = Phase::idle;
}

bool EncoderAutoDetect::feed(const RemoteEvent& event) {
    if (phase_ != Phase::turnLeft && phase_ != Phase::turnRight)
        return false;
    if (event.kind == RemoteEventKind::learnCandidate)
        return false; // a learn's candidate is not the user turning this control for us
    if (spec_.type != MessageType::cc || !specMatchesEvent(spec_, event))
        return false;
    (phase_ == Phase::turnLeft ? left_ : right_).push_back(event.rawValue);
    return true;
}

EncoderAutoDetect::Phase EncoderAutoDetect::advance() {
    if (phase_ == Phase::turnLeft) {
        phase_ = Phase::turnRight;
    } else if (phase_ == Phase::turnRight) {
        result_ = classify(left_, right_);
        phase_ = result_.has_value() ? Phase::done : Phase::undetermined;
    }
    return phase_;
}

int EncoderAutoDetect::sampleCount() const noexcept {
    if (phase_ == Phase::turnLeft)
        return static_cast<int>(left_.size());
    if (phase_ == Phase::turnRight)
        return static_cast<int>(right_.size());
    return 0;
}

// The three relative encodings differ in which side of 64 a direction lands on, and a pair of
// them differ only in how they spell "left" -- which is why the user turns BOTH ways:
//   relTwos       right = +n  (1, 2..)      left = 128-n (127, 126..)   left high, right low, left ~127
//   relSignMag    right = +n  (1, 2..)      left = 64+n  (65, 66..)     left high, right low, left ~65
//   relBinOffset  right = 64+n (65, 66..)   left = 64-n  (63, 62..)     left low, right high
//   abs7          right rises, left falls, values wander over the range (no fixed side of 64)
// The relative patterns are checked first: an absolute knob's two sweeps are contiguous, so they
// only ever fall on one side of 64 in a way that fails these tests (a leftward sweep ending above
// 64 is followed by a rightward sweep starting there, never one confined below 64).
std::optional<Encoding> EncoderAutoDetect::classify(const std::vector<int>& left, const std::vector<int>& right) {
    if (left.empty() || right.empty())
        return std::nullopt;

    const auto above = [](int v) { return v > 64; };
    const auto below = [](int v) { return v < 64; };

    if (allOf(left, above) && allOf(right, below)) {
        if (contains(left, 127))
            return Encoding::relTwos;
        if (contains(left, 65))
            return Encoding::relSignMag;
        return median(left) >= 96 ? Encoding::relTwos : Encoding::relSignMag;
    }
    if (allOf(left, below) && allOf(right, above))
        return Encoding::relBinOffset;

    const bool leftFalls = left.size() >= 2 && left.front() > left.back();
    const bool rightRises = right.size() >= 2 && right.front() < right.back();
    if (leftFalls && rightRises)
        return Encoding::abs7;
    return std::nullopt;
}

} // namespace synth::midi
