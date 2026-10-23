// SoundWaveformView.cpp -- the footer's waveform: the thumbnail, its draw-in, and the playhead that follows the
// preview player only while the sound plays.
#include "UI/Library/SoundBrowser/SoundWaveformView.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kStripHalfWidth = 3;
constexpr int kFrameHz = 30;
constexpr juce::uint32 kGiveUpMs = 4000; // a request that never starts (decode failed) stops being followed
} // namespace

SoundWaveformView::SoundWaveformView()
    : thumbnail_(256, formats_, cache_) {
    formats_.registerBasicFormats();
    setTitle("Waveform");
    setInterceptsMouseClicks(false, false);
    thumbnail_.addChangeListener(this);
}

SoundWaveformView::~SoundWaveformView() {
    thumbnail_.removeChangeListener(this);
    timer_.stopTimer();
    revealAnim_.stop(vblank_);
}

juce::Rectangle<int> SoundWaveformView::waveArea() const { return getLocalBounds().reduced(2, 2); }

void SoundWaveformView::setFile(const juce::File& file) {
    if (file == file_)
        return;
    file_ = file;
    lastPlayheadX_ = -1;
    if (file == juce::File()) {
        thumbnail_.clear();
        repaint();
        return;
    }
    thumbnail_.setSource(new juce::FileInputSource(file));
    startReveal();
}

void SoundWaveformView::startReveal() {
    const double ms = motionMs(190.0, 80.0);
    if (ms <= 0.0) {
        reveal_ = 1.0f;
        repaint();
        return;
    }
    reveal_ = 0.0f;
    revealAnim_.start(
        vblank_, ms, easeOutCubic,
        [this](float t) {
            reveal_ = t;
            repaint();
        },
        [this] {
            reveal_ = 1.0f;
            repaint();
        });
}

void SoundWaveformView::finishRevealForTest() {
    revealAnim_.completeForTest();
    reveal_ = 1.0f;
}

int SoundWaveformView::playheadX() const {
    if (player_ == nullptr || followedId_ == 0 || player_->playingRequestId() != followedId_)
        return -1;
    const auto area = waveArea();
    return area.getX() + juce::roundToInt(player_->playheadFraction() * (float)area.getWidth());
}

void SoundWaveformView::followRequest(std::uint64_t requestId) {
    followedId_ = requestId;
    sawPlaying_ = false;
    followStartedMs_ = juce::Time::getMillisecondCounter();
    timer_.startTimerHz(kFrameHz);
}

void SoundWaveformView::requestStrip(juce::Rectangle<int> strip) {
    if (onStripRepaintForTest)
        onStripRepaintForTest(strip);
    repaint(strip);
}

// One frame: repaint only the union of the old and new playhead strips, and stop once the sound has ended.
void SoundWaveformView::tick() {
    const int x = playheadX();
    const bool playing = x >= 0;
    if (playing)
        sawPlaying_ = true;
    if (x != lastPlayheadX_) {
        auto strip = juce::Rectangle<int>();
        for (const int px : {x, lastPlayheadX_})
            if (px >= 0)
                strip = strip.getUnion({px - kStripHalfWidth, 0, 2 * kStripHalfWidth + 1, getHeight()});
        if (!strip.isEmpty())
            requestStrip(strip);
        lastPlayheadX_ = x;
    }
    const bool gaveUp = !sawPlaying_ && juce::Time::getMillisecondCounter() - followStartedMs_ > kGiveUpMs;
    if ((sawPlaying_ && !playing) || gaveUp)
        timer_.stopTimer();
}

void SoundWaveformView::resized() { repaint(); }

void SoundWaveformView::paint(juce::Graphics& g) {
    const auto& c = synth::theme::themeOf(*this).colors;
    const auto area = waveArea();
    g.setColour(c.bg0);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour(c.border);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
    if (file_ == juce::File())
        return;
    {
        juce::Graphics::ScopedSaveState state(g);
        const bool slide = !prefersReducedMotion();
        if (slide)
            g.reduceClipRegion(area.withWidth(juce::roundToInt(reveal_ * (float)area.getWidth())));
        g.setColour(c.accent.withMultipliedAlpha(slide ? 1.0f : reveal_));
        if (thumbnail_.getTotalLength() > 0.0)
            thumbnail_.drawChannel(g, area, 0.0, thumbnail_.getTotalLength(), 0, 1.0f);
        else
            g.fillRect(area.getX(), area.getCentreY(), area.getWidth(), 1);
    }
    const int x = playheadX();
    if (x >= 0) {
        g.setColour(c.textPrimary);
        g.fillRect(x, 0, 2, getHeight());
    }
}

} // namespace synth::ui
