#pragma once

#include "../Modules/SamplerModule.h"
#include "Theme/AppLookAndFeel.h"
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
    , public juce::Timer {
public:
    explicit SampleWaveformComponent(SamplerModule& samplerModule)
        : sampler(samplerModule) {
        startTimerHz(15);
    }

    ~SampleWaveformComponent() override { stopTimer(); }

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
        juce::Colour bgColour = juce::Colours::black;
        juce::Colour waveColour = juce::Colours::limegreen;
        juce::Colour gridColour = juce::Colour(0xff2A2F38);
        juce::Colour mutedColour = juce::Colour(0xff5C6470);
        juce::Colour playheadColour = juce::Colours::white;

        if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
            const auto& colors = lf->getTheme().colors;
            bgColour = colors.bg1;
            waveColour = colors.accent;
            gridColour = colors.border.withAlpha(0.6f);
            mutedColour = colors.textDisabled;
            playheadColour = colors.textPrimary;
        }

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
        const float halfHeight = bounds.getHeight() * 0.45f;

        // One filled closed path (top edge left-to-right, bottom edge back again) rather than a
        // 1px drawVerticalLine per column. The canvas renders this component under GraphEditor's
        // zoom transform, so per-column lines do not tile at any zoom != 1 — they leave visible
        // gaps and moire striping. A single filled path scales cleanly.
        juce::Path waveform;
        const float columnWidth = bounds.getWidth() / (float)peaks.size();

        for (size_t col = 0; col < peaks.size(); ++col) {
            const float x = bounds.getX() + (float)col * columnWidth;
            const float top = midY - juce::jlimit(-1.0f, 1.0f, peaks[col].getEnd()) * halfHeight;
            if (col == 0)
                waveform.startNewSubPath(x, top);
            else
                waveform.lineTo(x, top);
        }
        for (size_t col = peaks.size(); col-- > 0;) {
            const float x = bounds.getX() + (float)col * columnWidth;
            const float bottom = midY - juce::jlimit(-1.0f, 1.0f, peaks[col].getStart()) * halfHeight;
            waveform.lineTo(x, bottom);
        }
        waveform.closeSubPath();

        g.setColour(waveColour);
        g.fillPath(waveform);
        // A near-silent sample collapses the path to a hairline; stroke the centre so it still reads
        // as "loaded but quiet" rather than as an empty box.
        g.drawHorizontalLine(juce::roundToInt(midY), bounds.getX(), bounds.getRight());

        if (sampler.isPlaying()) {
            g.setColour(playheadColour.withAlpha(0.8f));
            g.fillRect((float)currentPlayheadX(), bounds.getY(), 1.0f, bounds.getHeight());
        }
    }

private:
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleWaveformComponent)
};
