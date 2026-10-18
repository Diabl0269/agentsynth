#pragma once

// One pack in Settings > Sound packs: name, what is in it, its state, and the buttons for what can be done now.

#include "SoundPacks/SoundPackInstaller.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ProgressStrip.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class SoundPackRow : public juce::Component {
public:
    static constexpr int kHeight = 66; // padding, the name and two text lines

    /** Each is called with the pack id from a button click. */
    struct Actions {
        std::function<void(const juce::String&)> install, cancel, remove;
    };

    SoundPackRow(juce::String packId, Actions actions);

    /** Shows `row`'s state; the buttons and the bar fade where they stand. */
    void update(const synth::soundpacks::SoundPackInstaller::Row& row);

    const juce::String& getPackId() const noexcept { return packId_; }
    void resized() override;
    void paint(juce::Graphics& g) override;
    void lookAndFeelChanged() override;

    // ---- Test seams ----
    juce::TextButton& getDownloadButton() noexcept { return download_; }
    juce::TextButton& getUpdateButton() noexcept { return update_; }
    juce::TextButton& getRemoveButton() noexcept { return remove_; }
    juce::TextButton& getCancelButton() noexcept { return cancel_; }
    ProgressStrip& getProgress() noexcept { return progress_; }
    juce::String getStatusText() const { return status_.getText(); }
    juce::String getDetailText() const { return detail_.getText(); }
    juce::String getNameText() const { return name_.getText(); }
    bool isButtonShown(const juce::Button& button) const;

private:
    void showControls(const synth::soundpacks::SoundPackInstaller::Row& row);
    void applyColours();

    juce::String packId_;
    Actions actions_;
    bool hasError_ = false;
    juce::Label name_, detail_, status_;
    juce::TextButton download_{"Download"}, update_{"Update"}, remove_{"Remove"}, cancel_{"Cancel"};
    ProgressStrip progress_;
    FadeVisibility downloadFade_{&download_}, updateFade_{&update_}, removeFade_{&remove_},
        cancelFade_{&cancel_, &progress_};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundPackRow)
};

} // namespace synth::ui
