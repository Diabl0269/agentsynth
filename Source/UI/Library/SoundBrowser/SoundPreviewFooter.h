#pragma once

#include "Modules/Shapes/ShapeLibrary.h"
#include "SoundBrowser/SoundEntry.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Library/SoundBrowser/ShapeCurveView.h"
#include "UI/Library/SoundBrowser/SoundWaveformView.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

/** The lower part of the Sounds view's footer: the selected sound's waveform with its playhead, a Stop button, the
 *  preview's own volume and the Auto-play switch. For a selected shape it shows the shape's curve in a larger box
 *  instead and fades the transport out. It owns no audio: the browser tells it what plays. */
class SoundPreviewFooter : public juce::Component {
public:
    static constexpr int kHeight = 108;
    static constexpr int kCompactHeight = 36;

    SoundPreviewFooter();
    ~SoundPreviewFooter() override;

    /** Shows the waveform of `entry` (none clears it). */
    void showSound(const std::optional<synth::soundbrowser::SoundEntry>& entry);
    /** Shows `shape`'s curve in place of the waveform and hides Stop, Auto-play and the volume, fading over 120 ms;
     * none brings the transport back (the waveform is whatever showSound last set). */
    void showShape(const std::optional<synth::shapes::LibraryShape>& shape);
    /** A shape (not a sound) is what the footer is about; true from the moment showShape gets one. */
    bool isShowingShape() const { return shapeTarget_; }
    ShapeCurveView& getShapeCurve() { return curve_; }
    bool isShapeMotionRunning() const { return shapeAnim_.isRunning(); }
    void finishShapeMotionForTest();
    void setPlayer(const synth::soundbrowser::SoundPreviewPlayer* player) { wave_.setPlayer(player); }
    void followRequest(std::uint64_t requestId) { wave_.followRequest(requestId); }

    bool isAutoPlay() const { return autoPlay_.getToggleState(); }
    void setAutoPlay(bool on) { autoPlay_.setToggleState(on, juce::dontSendNotification); }
    float getGainDb() const { return (float)volume_.getValue(); }
    void setGainDb(float db) { volume_.setValue(db, juce::dontSendNotification); }

    /** The user flipped Auto-play, moved the volume or pressed Stop. */
    std::function<void(bool)> onAutoPlayChanged;
    std::function<void(float)> onGainChanged;
    std::function<void()> onStop;

    SoundWaveformView& getWaveform() { return wave_; }
    juce::Slider& getVolume() { return volume_; }
    juce::ToggleButton& getAutoPlay() { return autoPlay_; }
    juce::TextButton& getStopButton() { return stop_; }

    /** Compact is one row: Stop, then room for the sound's name (drawn by the browser), then the volume. The waveform
     *  and Auto-play are hidden, and keep their state. */
    void setCompact(bool compact);
    bool isCompact() const { return compact_; }
    /** In the footer's own coordinates; empty unless compact. */
    juce::Rectangle<int> getCompactNameBounds() const { return compactName_; }

    void resized() override;

private:
    void applyShapeMix();

    bool compact_ = false;
    bool shapeTarget_ = false;
    float shapeMix_ = 0.0f; // 0 = the sound's controls, 1 = the shape's picture
    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver shapeAnim_;
    ShapeCurveView curve_;
    juce::Rectangle<int> compactName_;
    SoundWaveformView wave_;
    juce::TextButton stop_{"Stop"};
    juce::ToggleButton autoPlay_{"Auto-play"};
    juce::Slider volume_{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundPreviewFooter)
};

} // namespace synth::ui
