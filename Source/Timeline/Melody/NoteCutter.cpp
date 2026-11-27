#include "Timeline/Melody/NoteCutter.h"

#include <algorithm>
#include <cmath>

namespace synth::melody {

namespace {

struct Sample {
    double time;
    float midi;
    float rmsDb;
};

float medianOf(std::vector<float> values) {
    if (values.empty())
        return 0.0f;
    std::sort(values.begin(), values.end());
    const auto n = values.size();
    return n % 2 == 1 ? values[n / 2] : 0.5f * (values[n / 2 - 1] + values[n / 2]);
}

// The voiced frames' pitch smoothed by a median over `width` neighbouring voiced frames.
std::vector<Sample> smoothedVoiced(const std::vector<PitchFrame>& frames, int width) {
    std::vector<const PitchFrame*> voiced;
    for (const auto& frame : frames)
        if (frame.voiced)
            voiced.push_back(&frame);
    std::vector<Sample> result;
    result.reserve(voiced.size());
    const int half = std::max(0, width / 2);
    for (int i = 0; i < (int)voiced.size(); ++i) {
        std::vector<float> window;
        for (int j = std::max(0, i - half); j <= std::min((int)voiced.size() - 1, i + half); ++j)
            window.push_back(voiced[(size_t)j]->midi);
        result.push_back({voiced[(size_t)i]->timeSec, medianOf(window), voiced[(size_t)i]->rmsDb});
    }
    return result;
}

// Builds the notes of one run of samples (a stretch with no gap longer than the bridge).
class RunCutter {
public:
    RunCutter(const NoteCutterSettings& settings, std::vector<CutNote>& out)
        : settings_(settings)
        , out_(out) {}

    void add(const Sample& sample) {
        if (current_.empty()) {
            current_.push_back(sample);
            return;
        }
        if (sample.time - current_.front().time >= settings_.settleSec) {
            if (!settled_) {
                settled_ = true;
                center_ = sample.midi; // the first frame past the scoop; recentre() refines it as the note goes on
            }
            if (std::abs(sample.midi - center_) > (float)settings_.splitSemitones) {
                if (departure_.empty())
                    firstDeparture_ = sample.time;
                departure_.push_back(sample);
                if (sample.time - firstDeparture_ >= settings_.splitHoldSec) {
                    splitAtDeparture();
                    return;
                }
                return;
            }
            // Back near the centre: the departure was a bend, not a note. Its frames belong to the note.
            for (const auto& held : departure_)
                current_.push_back(held);
            departure_.clear();
            current_.push_back(sample);
            recentre();
            return;
        }
        current_.push_back(sample);
    }

    void finish() {
        flush();
        departure_.clear();
        settled_ = false;
    }

private:
    void recentre() {
        std::vector<float> values;
        for (const auto& s : current_)
            if (s.time - current_.front().time >= settings_.settleSec)
                values.push_back(s.midi);
        if (!values.empty())
            center_ = medianOf(values);
    }

    void splitAtDeparture() {
        // The departure is seen only once the pitch is well past the old centre, partway through the glide into the
        // new note; the note really starts where the glide left the centre, so hand the glide's frames over too.
        std::vector<float> ahead;
        for (const auto& held : departure_)
            ahead.push_back(held.midi);
        const float direction = medianOf(ahead) > center_ ? 1.0f : -1.0f;
        std::size_t glideStart = current_.size();
        while (glideStart > 1 &&
               (current_[glideStart - 1].midi - center_) * direction > (float)settings_.glideSemitones)
            --glideStart;
        departure_.insert(departure_.begin(), current_.begin() + (std::ptrdiff_t)glideStart, current_.end());
        current_.erase(current_.begin() + (std::ptrdiff_t)glideStart, current_.end());
        flush();
        current_ = std::move(departure_);
        departure_.clear();
        settled_ = false;
        // The new note's own first stretch is exempt again, like any note start.
    }

    void flush() {
        std::vector<Sample> frames = std::move(current_);
        current_.clear();
        if (frames.empty())
            return;
        std::vector<float> pitches;
        double loud = 0.0;
        for (const auto& s : frames) {
            pitches.push_back(s.midi);
            loud += s.rmsDb;
        }
        const double start = frames.front().time - settings_.edgeCompensationSec;
        const double end = frames.back().time + settings_.edgeCompensationSec;
        if (end - start < settings_.minNoteSec)
            return;
        out_.push_back({std::max(0.0, start), end, medianOf(pitches), (float)(loud / (double)frames.size())});
    }

    const NoteCutterSettings& settings_;
    std::vector<CutNote>& out_;
    std::vector<Sample> current_;
    std::vector<Sample> departure_;
    double firstDeparture_ = 0.0;
    float center_ = 0.0f;
    bool settled_ = false;
};

} // namespace

// A departure that returns to the centre within the hold time is vibrato or a bend and stays in the note; one that
// holds is a new note starting where the departure began. Time between frames is measured from frame centres, so the
// hop is learnt from the data rather than assumed.
std::vector<CutNote> cutNotes(const std::vector<PitchFrame>& frames, const NoteCutterSettings& settings) {
    std::vector<CutNote> notes;
    const auto samples = smoothedVoiced(frames, settings.medianFrames);
    if (samples.empty())
        return notes;
    double hop = 0.01;
    if (frames.size() >= 2)
        hop = frames[1].timeSec - frames[0].timeSec;

    RunCutter cutter(settings, notes);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (i > 0 && samples[i].time - samples[i - 1].time > hop + settings.bridgeGapSec + 1.0e-6)
            cutter.finish();
        cutter.add(samples[i]);
    }
    cutter.finish();
    return notes;
}

} // namespace synth::melody
