// TimelineClipLaneWaveformTests.cpp — a recorded clip's waveform at three zooms: zoomed out (outline plus RMS body
// from the peaks), about one bucket per pixel (the switch-over, which must not jump) and zoomed in (the real wave
// from the file on disk), plus the raw-sample cache and the file-rate mapping. Pixel reads use a software image.
#include "../../../Timeline/WaveformTestSignals.h"
#include "TimelineClipLaneTestFixture.h"
#include "UI/Timeline/TimelineClipLaneArea/WaveformSampleCache.h"

namespace {

using synth::ui::TimelineClipLaneArea;
using Source = synth::ui::WaveformSource;
using namespace waveform_test;

constexpr double kFileRate = 48000.0; // deliberately not the 44.1 kHz fallback transport rate
constexpr double kBpm = 120.0;        // no transport in the fixture: 1 beat = 0.5 s = 24000 file samples
constexpr double kSecondsPerClip = 1.2;

struct WaveformScene {
    ClipLaneFixture f;
    juce::File wav = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_wfscene.wav");
    juce::File peaks =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_wfscene.agpk");
    std::vector<float> samples;
    synth::ClipId clip;
    int reads = 0;
    std::int64_t lastReadStart = -1;
    int lastReadNum = 0;
    int largestRead = 0;
    juce::Image baseline;

    explicit WaveformScene(double sourceStartSeconds = 0.0) {
        samples = makeNotes(kFileRate, 2.4);
        EXPECT_TRUE(writeWav(wav, kFileRate, samples));
        EXPECT_TRUE(synth::PeaksFile::writeForAudioFile(wav, peaks, 256));
        const auto track = f.doc.addTrack(TrackKind::Audio, "Audio 1");
        clip = f.doc.addClip(track, 0.0, kSecondsPerClip / (60.0 / kBpm), "");
        EXPECT_TRUE(f.doc.setClipAsset(clip, "Audio/take.wav", sourceStartSeconds));
        f.lane.setSize(1000, 200);
        baseline = paint();
    }
    ~WaveformScene() {
        wav.deleteFile();
        peaks.deleteFile();
    }

    void installPeaks() {
        f.lane.setPeaksResolver([this](const juce::String&) { return peaks; });
        f.lane.setAssetSampleRateResolver([](const juce::String&) { return kFileRate; });
    }
    void installSamples() {
        f.lane.setAssetSamplesResolver(
            [this](const juce::String&, std::int64_t start, int num, juce::AudioBuffer<float>& out) {
                ++reads;
                lastReadStart = start;
                lastReadNum = num;
                largestRead = std::max(largestRead, num);
                return synth::PeaksFile::readAudioSamples(wav, start, num, out);
            });
    }
    juce::Image paint() { return paintLaneSoftware(f.lane); }

    static juce::Image paintLaneSoftware(TimelineClipLaneArea& lane) {
        juce::Image image(juce::Image::ARGB, lane.getWidth(), lane.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        lane.paintEntireComponent(g, true);
        return image;
    }

    // Per pixel column, how far from the same lane without any waveform it is (darker or lighter), over the clip.
    std::vector<double> inkPerColumn(const juce::Image& img, const juce::Image& base, juce::Rectangle<int> rect) const {
        std::vector<double> ink;
        for (int x = rect.getX() + 2; x < std::min(rect.getRight() - 2, img.getWidth()); ++x) {
            double sum = 0.0;
            for (int y = rect.getY() + 1; y < rect.getBottom() - 1; ++y)
                sum += std::abs(base.getPixelAt(x, y).getBrightness() - img.getPixelAt(x, y).getBrightness());
            ink.push_back(sum);
        }
        return ink;
    }
    std::vector<double> inkFor(juce::Image& base, juce::Rectangle<int> rect) {
        return inkPerColumn(paint(), base, rect);
    }
};

juce::Image baselineAt(WaveformScene& s, double pixelsPerBeat) {
    s.f.state.pixelsPerBeat = pixelsPerBeat;
    s.f.lane.refreshFromDoc();
    return s.paint();
}

double mean(const std::vector<double>& v) {
    double sum = 0.0;
    for (double d : v)
        sum += d;
    return v.empty() ? 0.0 : sum / (double)v.size();
}

} // namespace

TEST(TimelineClipLaneWaveformZoom, ZoomedOutDrawsPeaksOutlineAndBodyWithoutReadingTheFile) {
    WaveformScene s;
    const auto base = baselineAt(s, 10.0); // 2400 samples per pixel
    s.installPeaks();
    s.installSamples();
    const auto img = s.paint();
    EXPECT_EQ(s.f.lane.getLastWaveformSourceForTest(), Source::Peaks);
    EXPECT_EQ(s.reads, 0) << "zoomed out never reads the audio file";
    const auto rect = s.f.lane.getClipRect(s.clip);
    EXPECT_GT(mean(s.inkPerColumn(img, base, rect)), 1.0) << "a waveform was drawn";
}

TEST(TimelineClipLaneWaveformZoom, InterpolatedPeaksHaveNoStairsWhenSamplesAreUnavailable) {
    // Regression test for FRO1733: columns > buckets repeated one bucket as flat 1-pixel steps.
    WaveformScene s;
    const auto base = baselineAt(s, 375.0); // 64 samples per pixel, 4 columns per bucket
    s.installPeaks();                       // no samples resolver: the peaks are all there is
    const auto img = s.paint();
    EXPECT_EQ(s.f.lane.getLastWaveformSourceForTest(), Source::Peaks);
    const auto ink = s.inkPerColumn(img, base, s.f.lane.getClipRect(s.clip));
    ASSERT_GT(ink.size(), 400u);
    EXPECT_LE(longestRun(ink, 1e-3), 3) << "one bucket repeated across columns would repeat the same ink";
}

TEST(TimelineClipLaneWaveformZoom, SwitchOverToTheRealWaveDoesNotJump) {
    WaveformScene s;
    const auto baseOut = baselineAt(s, 93.0); // 258 samples per pixel: peaks
    const auto baseIn = baselineAt(s, 95.0);  // 253 samples per pixel: real samples
    s.installPeaks();
    s.installSamples();
    s.f.state.pixelsPerBeat = 93.0;
    s.f.lane.refreshFromDoc();
    const auto imgOut = s.paint();
    EXPECT_EQ(s.f.lane.getLastWaveformSourceForTest(), Source::Peaks);
    const double outInk = mean(s.inkPerColumn(imgOut, baseOut, s.f.lane.getClipRect(s.clip)));

    s.f.state.pixelsPerBeat = 95.0;
    s.f.lane.refreshFromDoc();
    const auto imgIn = s.paint();
    EXPECT_EQ(s.f.lane.getLastWaveformSourceForTest(), Source::SampleColumns);
    const double inInk = mean(s.inkPerColumn(imgIn, baseIn, s.f.lane.getClipRect(s.clip)));

    ASSERT_GT(outInk, 1.0);
    EXPECT_NEAR(inInk / outInk, 1.0, 0.12) << "the amount of waveform drawn must not jump at the switch";
}

TEST(TimelineClipLaneWaveformZoom, ZoomedInToSingleCyclesDrawsTheRealWaveLine) {
    WaveformScene s;
    const auto base = baselineAt(s, 20000.0); // 1.2 samples per pixel
    s.installPeaks();
    s.installSamples();
    const auto img = s.paint();
    EXPECT_EQ(s.f.lane.getLastWaveformSourceForTest(), Source::SampleLine);
    EXPECT_LE(s.largestRead, 1 << 20);

    // The line: the topmost inked row per column rises and falls with each cycle (a filled band would not).
    const auto rect = s.f.lane.getClipRect(s.clip);
    std::vector<int> top;
    for (int x = 2; x < 998; ++x)
        for (int y = rect.getY() + 1; y < rect.getBottom() - 1; ++y)
            if (std::abs(base.getPixelAt(x, y).getBrightness() - img.getPixelAt(x, y).getBrightness()) > 0.1f) {
                top.push_back(y);
                break;
            }
    ASSERT_GT(top.size(), 900u);
    int turns = 0, lastSign = 0;
    for (std::size_t i = 1; i < top.size(); ++i) {
        const int sign = (top[i] > top[i - 1]) - (top[i] < top[i - 1]);
        if (sign != 0 && lastSign != 0 && sign != lastSign)
            ++turns;
        if (sign != 0)
            lastSign = sign;
    }
    EXPECT_GE(turns, 8) << "about eleven cycles of 220 Hz fit in 1000 pixels at this zoom";
    // A thin line, not a filled band: well under half of the clip's pixels carry ink.
    int inked = 0, total = 0;
    for (int x = 2; x < 998; ++x)
        for (int y = rect.getY() + 1; y < rect.getBottom() - 1; ++y, ++total)
            inked +=
                std::abs(base.getPixelAt(x, y).getBrightness() - img.getPixelAt(x, y).getBrightness()) > 0.05f ? 1 : 0;
    EXPECT_LT(inked, total * 0.35);
}

TEST(TimelineClipLaneWaveformZoom, BetweenTheLineAndTheSwitchOverTheExactColumnsAreDrawn) {
    WaveformScene s;
    baselineAt(s, 1000.0); // 24 samples per pixel
    s.installPeaks();
    s.installSamples();
    s.paint();
    EXPECT_EQ(s.f.lane.getLastWaveformSourceForTest(), Source::SampleColumns);
}

TEST(TimelineClipLaneWaveformZoom, ZoomingBackOutReturnsToThePeaks) {
    WaveformScene s;
    s.installPeaks();
    s.installSamples();
    baselineAt(s, 20000.0);
    s.paint();
    ASSERT_EQ(s.f.lane.getLastWaveformSourceForTest(), Source::SampleLine);
    baselineAt(s, 40.0);
    s.paint();
    EXPECT_EQ(s.f.lane.getLastWaveformSourceForTest(), Source::Peaks);
}

TEST(TimelineClipLaneWaveformSamples, ReadsAreMappedWithTheAssetRateAndStayBounded) {
    // The clip starts 1 s into a 48 kHz file under the 44.1 kHz fallback transport: the block read must hold
    // sample 48000, not 44100.
    WaveformScene s(1.0);
    baselineAt(s, 20000.0);
    s.installPeaks();
    s.installSamples();
    s.paint();
    ASSERT_EQ(s.reads, 1);
    EXPECT_LE(s.lastReadStart, 48000);
    EXPECT_GE(s.lastReadStart + s.lastReadNum, 48000 + 1000);
    EXPECT_GT(s.lastReadStart, 44100 + 1000) << "mapped at the transport rate instead of the file's";
    EXPECT_LE(s.lastReadNum, 1 << 20);
}

TEST(TimelineClipLaneWaveformSamples, SampleSpanUsesTheFileRate) {
    const auto at48 = synth::ui::sampleSpanForClip(48000.0, 1.0, 4.0, 120.0);
    const auto at44 = synth::ui::sampleSpanForClip(44100.0, 1.0, 4.0, 120.0);
    EXPECT_DOUBLE_EQ(at48.startSample, 48000.0);
    EXPECT_DOUBLE_EQ(at48.numSamples, 96000.0);
    EXPECT_DOUBLE_EQ(at44.startSample, 44100.0);
    EXPECT_NE(at48.startSample, at44.startSample);
}

TEST(TimelineClipLaneWaveformSamples, ASecondPaintAndAScrollNeverTouchTheDiskAgain) {
    WaveformScene s;
    baselineAt(s, 20000.0);
    s.installPeaks();
    s.installSamples();
    s.paint();
    ASSERT_EQ(s.reads, 1);
    s.paint();
    EXPECT_EQ(s.reads, 1) << "the cached block serves a repaint";
    s.f.state.firstVisibleBeat = 0.0002; // 4 px: the view still inside the padded block
    s.paint();
    EXPECT_EQ(s.reads, 1);
    s.f.lane.invalidatePeaksCache();
    s.paint();
    EXPECT_EQ(s.reads, 2) << "a cache clear forgets the block";
}

TEST(TimelineClipLaneWaveformSamples, AFailedReadFallsBackToThePeaksAndIsNotRetried) {
    WaveformScene s;
    baselineAt(s, 20000.0);
    s.installPeaks();
    int attempts = 0;
    s.f.lane.setAssetSamplesResolver([&](const juce::String&, std::int64_t, int, juce::AudioBuffer<float>&) {
        ++attempts;
        return false;
    });
    s.paint();
    EXPECT_EQ(s.f.lane.getLastWaveformSourceForTest(), Source::Peaks);
    s.paint();
    EXPECT_EQ(attempts, 1);
}

TEST(TimelineClipLaneWaveformSamples, AMissingAssetNeverReadsSamples) {
    WaveformScene s;
    baselineAt(s, 20000.0);
    s.installPeaks();
    s.installSamples();
    s.f.lane.setAssetExistsResolver([](const juce::String&) { return false; });
    s.paint();
    EXPECT_EQ(s.reads, 0);
}

TEST(TimelineClipLaneWaveformSamples, TheLiveStripDrawsItsPeaksAndNeverReadsTheFile) {
    WaveformScene s;
    s.installSamples(); // the committed clip has no peaks here, so only the live strip draws
    const auto data = peaksOf(makeNotes(kFileRate, 0.22));
    bool withPeaks = false;
    TimelineClipLaneArea::LiveRecordingInfo info;
    info.active = true;
    info.track = s.f.doc.getTracks().front().id;
    info.punchBeat = 0.0;
    info.currentBeat = 3.0;
    info.numChannels = 1;
    info.copyPeaks = [&](std::vector<std::pair<float, float>>& peaks, std::vector<float>& rms) {
        peaks = withPeaks ? data.buckets : std::vector<std::pair<float, float>>{};
        rms = withPeaks ? data.rms : std::vector<float>{};
    };
    s.f.state.pixelsPerBeat = 300.0;
    s.f.lane.updateLiveRecording(info);
    const auto empty = s.paint();
    withPeaks = true;
    s.f.lane.updateLiveRecording(info);
    const auto img = s.paint();
    EXPECT_EQ(s.reads, 0) << "a live take's file is still being written";
    EXPECT_EQ(s.f.lane.getLastWaveformSourceForTest(), Source::None);
    double inked = 0.0;
    for (int x = 6; x < 200; ++x)
        for (int y = 0; y < img.getHeight(); ++y)
            inked += std::abs(img.getPixelAt(x, y).getBrightness() - empty.getPixelAt(x, y).getBrightness());
    EXPECT_GT(inked, 50.0) << "the strip draws the peaks it was given";
}

namespace {
// Per-column ink of the real-sample drawing against the interpolated-peaks drawing of the same clip at the same
// zoom, as a mean absolute difference relative to the mean ink. A wrong file-to-sample mapping (the transport rate
// instead of the file's, a missing source offset) shifts the changing notes against the peaks and makes it large.
double samplesVersusPeaksDifference(double sourceStartSeconds, double pixelsPerBeat) {
    WaveformScene s(sourceStartSeconds);
    const auto base = baselineAt(s, pixelsPerBeat);
    s.installPeaks();
    s.installSamples();
    const auto rect = s.f.lane.getClipRect(s.clip);
    const auto withSamples = s.inkPerColumn(s.paint(), base, rect);
    EXPECT_EQ(s.f.lane.getLastWaveformSourceForTest(), Source::SampleColumns);
    s.f.lane.setAssetSamplesResolver(
        [](const juce::String&, std::int64_t, int, juce::AudioBuffer<float>&) { return false; });
    const auto withPeaks = s.inkPerColumn(s.paint(), base, rect);
    EXPECT_EQ(s.f.lane.getLastWaveformSourceForTest(), Source::Peaks);
    EXPECT_EQ(withSamples.size(), withPeaks.size());
    double diff = 0.0;
    for (std::size_t i = 0; i < std::min(withSamples.size(), withPeaks.size()); ++i)
        diff += std::abs(withSamples[i] - withPeaks[i]);
    return diff / (double)std::max<std::size_t>(1, withPeaks.size()) / std::max(1e-9, mean(withPeaks));
}
} // namespace

TEST(TimelineClipLaneWaveformZoom, RealSamplesAgreeWithThePeaksJustBelowTheSwitchOver) {
    // 253 samples per pixel against 256-sample buckets, on a 48 kHz file under the 44.1 kHz fallback transport,
    // from the file start and from one second in.
    EXPECT_LT(samplesVersusPeaksDifference(0.0, 95.0), 0.2);
    EXPECT_LT(samplesVersusPeaksDifference(1.0, 95.0), 0.2);
}
