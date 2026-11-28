#pragma once

#include "UI/Layout/FadeVisibility.h"
#include "UI/Mixer/MixerHeader/MixerIconButton.h"

// PluginOffBadge.h (docs/architecture/plugin-layer.md#when-a-plugin-takes-the-app-down): the small badge a track
// header and a mixer column header show while a plugin on that track is switched off after a crash ("Open without
// it"). Same glyph, same words on both, so the two read as one fact. A Tab stop that acts on Return and Space; the
// owner decides what a press does (both show the plugin's card, where its Turn on button is). It fades in and out
// with the app's soft fade (a plain fade under Reduce Motion, none when Animations is off). Runtime only: the badge
// follows HostedPluginModule::isSwitchedOffAfterCrash() and is never saved.
namespace synth::ui {

class PluginOffBadge : public MixerIconButton {
public:
    static constexpr int kSize = 20; // square hit area, px; the same as the track header's other small buttons

    PluginOffBadge();

    /** The sentence the tooltip, the screen-reader title and the description all carry; empty for no plugins. */
    static juce::String wordsFor(const juce::StringArray& pluginTitles);

    /** Names the switched-off plugins and fades the badge in, or out when `pluginTitles` is empty. `snap` lands at once
     *  (a row built with the screen, not one that changed under the user's eye). */
    void setPlugins(const juce::StringArray& pluginTitles, bool snap = false);
    bool isShownLogically() const noexcept { return fade_.isShown(); }
    /** 0 hidden to 1 shown, for an owner that opens its slot with the fade. */
    float progress() const { return fade_.progress(); }
    /** HostedPluginModule::switchedOffRevision() when an owner last read the plugins, so it re-reads only on a change.
     */
    unsigned seenRevision = 0;
    /** Called each frame of a running fade, so the owner can lay out again. */
    void setOnFade(std::function<void()> onFrame) { fade_.onFrame = std::move(onFrame); }

private:
    juce::StringArray titles_;
    FadeVisibility fade_{this};
};

} // namespace synth::ui
