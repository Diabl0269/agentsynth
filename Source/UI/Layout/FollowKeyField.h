#pragma once

#include "Music/MusicalScale.h"
#include "UI/Layout/KeyField.h"
#include "UI/Layout/KeyPicker/KeyAuditioner.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace synth::ui {

class KeyPicker; // Source/UI/Layout/KeyPicker/KeyPicker.h

/** The Key field of something that follows the song key or has its own: reads "Song key · D Dorian" in the muted
 *  colour while following and "A Phrygian · own" otherwise, and opens the key picker with a Follow song key row on
 *  top. Hovering a key in its picker plays the scale (KeyAuditioner). When the song key it follows changes, it glows
 * once. docs/timeline/song-key.md#following-the-song-key. */
class FollowKeyField : public KeyField {
public:
    FollowKeyField();
    ~FollowKeyField() override;

    /** Shows the state. `songKey` is what a follower plays (nullopt = No key); `ownKey` is its own key (nullopt = no
     *  scale). The first call never glows. */
    void setState(bool following, const std::optional<MusicalScale>& songKey,
                  const std::optional<MusicalScale>& ownKey);
    bool isFollowing() const noexcept { return following_; }

    /** Offered as "Your scales" in the picker. */
    void setUserScales(std::vector<UserScale> scales) { userScales_ = std::move(scales); }

    /** An own key was picked (nullopt = No scale). */
    std::function<void(std::optional<MusicalScale>)> onPickOwn;
    /** Follow song key was chosen. */
    std::function<void()> onFollow;
    /** Launches the picker; unset = a CallOutBox from the field. Tests replace it to keep the picker in hand. */
    std::function<void(std::unique_ptr<juce::Component> picker, juce::Rectangle<int> screenArea)> pickerLauncher;

    /** Plays the scale of a hovered key; its owner sets where (`resolveTarget`), what the default sound is and where
     *  the on/off setting is read. */
    KeyAuditioner& getAuditioner() noexcept { return auditioner_; }

    /** Opens the key picker (the field's click, Space, Return or Down). */
    void openPicker();
    /** The open picker, or null. */
    KeyPicker* getOpenPicker() const noexcept;

    /** 0..1: how strongly the field glows now. */
    float getGlow() const noexcept { return glow_; }
    /** Test seam: lands a running glow on its final frame. */
    void finishGlowForTest();

    static constexpr double kGlowMs = 300.0;

    void paintOverChildren(juce::Graphics& g) override;
    void lookAndFeelChanged() override;

private:
    void refreshLook();
    void startGlow();

    bool seeded_ = false;
    bool following_ = true;
    std::optional<MusicalScale> songKey_;
    std::optional<MusicalScale> ownKey_;
    std::vector<UserScale> userScales_;
    KeyAuditioner auditioner_;
    juce::Component::SafePointer<juce::Component> openPicker_;

    float glow_ = 0.0f;
    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver glowAnim_;

    JUCE_DECLARE_NON_COPYABLE(FollowKeyField)
};

} // namespace synth::ui
