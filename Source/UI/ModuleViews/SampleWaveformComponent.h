#pragma once

#include "Modules/SamplerModule.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

/**
 * Waveform overview + playhead for a SamplerModule.
 *
 * Peaks are computed once per (sample, width) pair and cached — the 15 Hz timer only repaints when
 * the loaded sample changes or the playhead crosses a whole pixel, so an idle or stopped Sampler
 * costs nothing. That gating is deliberate: ModuleComponent is setBufferedToImage(true), so an
 * unconditional child repaint would invalidate the parent's cached image on every tick.
 */
class SampleWaveformComponent
    : public juce::Component
    , public juce::SettableTooltipClient
    , public juce::Timer {
public:
    explicit SampleWaveformComponent(SamplerModule& samplerModule)
        : sampler(samplerModule) {
        setTitle("Sample waveform");
        setDescription("Waveform overview of the loaded sample with the playhead");
        setTooltip("Sample waveform: the loaded sample and its playhead");
        startTimerHz(15);
    }

    ~SampleWaveformComponent() override {
        stopTimer();
        revealAnim_.stop(vblank_);
    }

    /** Call right after loading a new sample: the old picture fades out while the new one draws in from the left over
     *  220 ms (an 80 ms cross-fade under Reduce Motion). The picture lands at once with Animations off, while the card
     *  is not showing, or when the load changed nothing. */
    void playReplaceMotion() {
        const auto before = peaks;
        const int generationBefore = cachedGeneration;
        refreshPeaksIfStale();
        const double ms = synth::ui::motionMs(220.0, 80.0);
        if (cachedGeneration == generationBefore || peaks.empty() || ms <= 0.0 ||
            (!isShowing() && !animateOffscreen_)) {
            repaint();
            return;
        }
        outgoing_ = before;
        reveal_ = 0.0f;
        revealAnim_.start(
            vblank_, ms, synth::ui::easeOutCubic,
            [this](float t) {
                reveal_ = t;
                repaint();
            },
            [this] {
                reveal_ = 1.0f;
                outgoing_.clear();
                repaint();
            });
    }

    /** 0 while the new picture is not drawn yet, 1 when it is fully drawn. */
    float getReveal() const noexcept { return reveal_; }
    bool isRevealRunning() const noexcept { return revealAnim_.isRunning(); }
    /** Test seams: let the motion run while the card is off screen, step it by hand, or land it. */
    void setAnimateOffscreenForTest(bool on) noexcept { animateOffscreen_ = on; }
    void stepRevealForTest(double ms) { revealAnim_.stepForTest(ms); }
    void finishRevealForTest() { revealAnim_.completeForTest(); }

    // -------------------------------------------------------------------------
    // Static helper (testable without a display)
    // -------------------------------------------------------------------------

    /** Reduces `audio` to one min/max pair per column, averaging all channels.
     *  Returns an empty vector when there is nothing to draw. */
    static std::vector<juce::Range<float>> computePeaks(const juce::AudioBuffer<float>& audio, int numColumns) {
        std::vector<juce::Range<float>> peaks;
        const int numFrames = audio.getNumSamples();
        const int numChannels = audio.getNumChannels();
        if (numColumns <= 0 || numFrames <= 0 || numChannels <= 0)
            return peaks;

        peaks.reserve((size_t)numColumns);
        for (int col = 0; col < numColumns; ++col) {
            const int from = (int)((juce::int64)col * numFrames / numColumns);
            const int to = juce::jmax(from + 1, (int)((juce::int64)(col + 1) * numFrames / numColumns));

            float minValue = 0.0f, maxValue = 0.0f;
            for (int i = from; i < to && i < numFrames; ++i) {
                float mixed = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    mixed += audio.getSample(ch, i);
                mixed /= (float)numChannels;
                minValue = juce::jmin(minValue, mixed);
                maxValue = juce::jmax(maxValue, mixed);
            }
            peaks.push_back({minValue, maxValue});
        }
        return peaks;
    }

    // -------------------------------------------------------------------------
    // Component / Timer
    // -------------------------------------------------------------------------

    void resized() override {
        cachedWidth = -1; // force a peak rebuild at the new column count
        refreshPeaksIfStale();
    }

    void timerCallback() override {
        const int generation = sampler.getSampleGeneration();
        const int playheadX = currentPlayheadX();

        if (generation != cachedGeneration || getWidth() != cachedWidth) {
            refreshPeaksIfStale();
            lastPlayheadX = playheadX;
            repaint();
            return;
        }

        if (playheadX != lastPlayheadX) {
            lastPlayheadX = playheadX;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override {
        const auto& colors = synth::theme::themeOf(*this).colors;
        const auto bgColour = colors.bg1;
        const auto waveColour = colors.accent;
        const auto gridColour = colors.border.withAlpha(0.6f);
        const auto mutedColour = colors.textDisabled;
        const auto playheadColour = colors.textPrimary;

        auto bounds = getLocalBounds().toFloat();
        g.fillAll(bgColour);

        g.setColour(gridColour);
        g.drawHorizontalLine(juce::roundToInt(bounds.getCentreY()), bounds.getX(), bounds.getRight());

        if (peaks.empty()) {
            g.setColour(mutedColour);
            g.setFont(12.0f);
            g.drawText("No sample loaded", bounds, juce::Justification::centred, false);
            return;
        }

        const float midY = bounds.getCentreY();
        const bool moving = reveal_ < 1.0f;
        if (moving && !outgoing_.empty()) {
            g.setColour(waveColour.withAlpha(1.0f - reveal_));
            g.fillPath(buildWaveformPath(outgoing_, bounds));
        }
        {
            juce::Graphics::ScopedSaveState state(g);
            const bool wipe = moving && !synth::ui::prefersReducedMotion();
            if (wipe)
                g.reduceClipRegion(bounds.withWidth(reveal_ * bounds.getWidth()).getSmallestIntegerContainer());
            g.setColour(waveColour.withMultipliedAlpha(moving && !wipe ? reveal_ : 1.0f));
            g.fillPath(buildWaveformPath(peaks, bounds));
            // A near-silent sample collapses the path to a hairline; stroke the centre so it still reads
            // as "loaded but quiet" rather than as an empty box.
            g.drawHorizontalLine(juce::roundToInt(midY), bounds.getX(), bounds.getRight());
        }

        if (sampler.isPlaying()) {
            g.setColour(playheadColour.withAlpha(0.8f));
            g.fillRect((float)currentPlayheadX(), bounds.getY(), 1.0f, bounds.getHeight());
        }
    }

private:
    // One filled closed path (top edge left-to-right, bottom edge back again) rather than a
    // 1px drawVerticalLine per column. The canvas renders this component under GraphEditor's
    // zoom transform, so per-column lines do not tile at any zoom != 1 - they leave visible
    // gaps and moire striping. A single filled path scales cleanly.
    static juce::Path buildWaveformPath(const std::vector<juce::Range<float>>& columns, juce::Rectangle<float> bounds) {
        juce::Path waveform;
        const float midY = bounds.getCentreY();
        const float halfHeight = bounds.getHeight() * 0.45f;
        const float columnWidth = bounds.getWidth() / (float)columns.size();
        for (size_t col = 0; col < columns.size(); ++col) {
            const float x = bounds.getX() + (float)col * columnWidth;
            const float top = midY - juce::jlimit(-1.0f, 1.0f, columns[col].getEnd()) * halfHeight;
            if (col == 0)
                waveform.startNewSubPath(x, top);
            else
                waveform.lineTo(x, top);
        }
        for (size_t col = columns.size(); col-- > 0;) {
            const float x = bounds.getX() + (float)col * columnWidth;
            waveform.lineTo(x, midY - juce::jlimit(-1.0f, 1.0f, columns[col].getStart()) * halfHeight);
        }
        waveform.closeSubPath();
        return waveform;
    }

    int currentPlayheadX() const {
        const int width = getWidth();
        if (width <= 1)
            return 0;
        return juce::jlimit(0, width - 1, juce::roundToInt(sampler.getPlayheadPosition() * (float)(width - 1)));
    }

    void refreshPeaksIfStale() {
        const int generation = sampler.getSampleGeneration();
        const int width = getWidth();
        if (generation == cachedGeneration && width == cachedWidth)
            return;

        cachedGeneration = generation;
        cachedWidth = width;

        if (auto sample = sampler.getSample())
            peaks = computePeaks(sample->audio, width);
        else
            peaks.clear();
    }

    SamplerModule& sampler;
    std::vector<juce::Range<float>> peaks;
    int cachedGeneration = -1;
    int cachedWidth = -1;
    int lastPlayheadX = -1;
    std::vector<juce::Range<float>> outgoing_; // the picture being replaced, while it fades out
    float reveal_ = 1.0f;
    bool animateOffscreen_ = false;
    juce::VBlankAnimatorUpdater vblank_{this};
    synth::ui::AnimationDriver revealAnim_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleWaveformComponent)
};
