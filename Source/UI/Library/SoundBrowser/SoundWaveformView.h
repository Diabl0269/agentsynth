#pragma once

#include "SoundBrowser/SoundPreviewPlayer.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

/** The waveform of the selected sound with the preview's playhead. The picture is built by a JUCE audio thumbnail on
 * its own thread, and draws in from the left over 190 ms (a fade over 80 ms under Reduce Motion). While a preview of
 * this sound plays, a 30 Hz timer moves the playhead and repaints only the strip it crosses; it stops with the sound.
 */
class SoundWaveformView
    : public juce::Component
    , private juce::ChangeListener {
public:
    SoundWaveformView();
    ~SoundWaveformView() override;

    /** Shows `file`'s waveform and draws it in; an empty file clears the view. */
    void setFile(const juce::File& file);
    /** The preview whose playhead to follow (may be null). */
    void setPlayer(const synth::soundbrowser::SoundPreviewPlayer* player) { player_ = player; }
    /** A preview of the shown sound was requested: follow the player until that request ends. */
    void followRequest(std::uint64_t requestId);

    /** 0 while the picture is not drawn, 1 when fully drawn. */
    float getReveal() const { return reveal_; }
    bool isThumbnailReadyForTests() const { return thumbnail_.isFullyLoaded(); }
    /** The x of the playhead in this component's coordinates, or -1 when none shows. */
    int playheadX() const;
    bool isFollowing() const { return timer_.isTimerRunning(); }
    bool isRevealRunning() const { return revealAnim_.isRunning(); }
    void finishRevealForTest();
    /** Test seams: run one timer frame by hand, and see every strip a frame asks to repaint. */
    void tickForTest() { tick(); }
    std::function<void(juce::Rectangle<int>)> onStripRepaintForTest;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class FrameTimer : public juce::Timer {
    public:
        explicit FrameTimer(SoundWaveformView& owner)
            : owner_(owner) {}
        void timerCallback() override { owner_.tick(); }

    private:
        SoundWaveformView& owner_;
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }
    void tick();
    void requestStrip(juce::Rectangle<int> strip);
    void startReveal();
    juce::Rectangle<int> waveArea() const;

    const synth::soundbrowser::SoundPreviewPlayer* player_ = nullptr;
    juce::AudioFormatManager formats_;
    juce::AudioThumbnailCache cache_{4};
    juce::AudioThumbnail thumbnail_;
    juce::File file_;
    float reveal_ = 1.0f;
    std::uint64_t followedId_ = 0;
    bool sawPlaying_ = false;
    juce::uint32 followStartedMs_ = 0;
    int lastPlayheadX_ = -1;
    FrameTimer timer_{*this};
    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver revealAnim_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundWaveformView)
};

} // namespace synth::ui
