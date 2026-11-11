#pragma once

// One pack in Settings > Sound packs: name, what is in it, its state, and the buttons for what can be done now.
// A chevron opens the row to show who made the pack, its licence and every instrument.

#include "SoundPacks/SoundPackInstaller.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ProgressStrip.h"
#include "UI/Settings/SoundPacks/SoundPackCredit.h"
#include "UI/Settings/SoundPacks/SoundPackDisclosure.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class SoundPackRow : public juce::Component {
public:
    static constexpr int kHeight = 66; // padding, the name and two text lines

    /** Each is called with the pack id from a button click (openLicence with the licence address). */
    struct Actions {
        std::function<void(const juce::String&)> install, cancel, remove;
        std::function<void(const juce::String& url)> openLicence;
        std::function<void(const juce::String& packId)> showSources;
    };

    SoundPackRow(juce::String packId, Actions actions);

    /** Shows `row`'s state; the buttons and the bar fade where they stand. */
    void update(const synth::soundpacks::SoundPackInstaller::Row& row);

    /** The row's height at `width`: kHeight closed, taller by the details when open, and in between while they fade
     *  in or out (the height glides with the credit's fade). */
    int heightForWidth(int width) const;
    bool isExpanded() const noexcept { return disclosure_.isOpen(); }
    /** Opens or closes the details: they fade and the chevron turns, or both land at once when `animate` is false.
     *  Calls onExpandedChanged when the state changed. */
    void setExpanded(bool expanded, bool animate = true);
    /** Called after the open state changed; the owner lays the list out again and remembers the state. */
    std::function<void()> onExpandedChanged;
    /** Called on each frame of the glide, when heightForWidth() has moved; the owner lays the list out again. */
    std::function<void()> onHeightChanged;

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
    juce::Rectangle<int> getDetailBounds() const { return detail_.getBounds(); }
    juce::String getDetailTooltip() { return detail_.getTooltip(); }
    juce::String getNameText() const { return name_.getText(); }
    bool isButtonShown(const juce::Button& button) const;
    SoundPackDisclosure& getDisclosure() noexcept { return disclosure_; }
    SoundPackCredit& getCredit() noexcept { return credit_; }
    bool isCreditShown() const { return creditFade_.isShown(); }

private:
    void showControls(const synth::soundpacks::SoundPackInstaller::Row& row);
    void applyColours();
    /** The line shows every instrument; when it is wider than its label (elided with ...) the full text is its tooltip.
     */
    void fitDetail();

    juce::String packId_;
    Actions actions_;
    bool hasError_ = false;
    juce::Label name_, detail_, status_;
    juce::TextButton download_{"Download"}, update_{"Update"}, remove_{"Remove"}, cancel_{"Cancel"};
    ProgressStrip progress_;
    SoundPackDisclosure disclosure_;
    SoundPackCredit credit_;
    FadeVisibility downloadFade_{&download_}, updateFade_{&update_}, removeFade_{&remove_},
        cancelFade_{&cancel_, &progress_};
    FadeVisibility creditFade_{&credit_};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundPackRow)
};

} // namespace synth::ui
