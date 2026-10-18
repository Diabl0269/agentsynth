#pragma once

// The Settings "Sound packs" tab: every downloadable and installed pack as a row with its state and buttons, then a
// Credits section naming who made each pack's recordings. Progress shown here is the shared installer's, so a download
// started from the Sampler's instrument picker appears here too.

#include "SoundPacks/SoundPackInstaller.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/TextLinkButton.h"
#include "UI/Settings/SoundPacks/SoundPackCredit.h"
#include "UI/Settings/SoundPacks/SoundPackRow.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class SoundPacksSettingsTab
    : public juce::Component
    , private synth::soundpacks::SoundPackInstaller::Listener {
public:
    /** The installer must outlive the tab. Fetches the pack list each time the tab is shown. */
    explicit SoundPacksSettingsTab(synth::soundpacks::SoundPackInstaller& installer);
    ~SoundPacksSettingsTab() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void lookAndFeelChanged() override;

    // ---- Test seams ----
    void setUrlOpenerForTesting(std::function<void(const juce::URL&)> opener) { urlOpener_ = std::move(opener); }
    /** Replaces the Remove question: receives the wording and the action a "yes" runs. */
    void setConfirmerForTesting(std::function<void(const juce::String&, std::function<void()>)> confirmer) {
        confirmer_ = std::move(confirmer);
    }
    int getRowCountForTest() const { return (int)rows_.size(); }
    synth::ui::SoundPackRow& getRowForTest(int index) { return *rows_[(size_t)index]; }
    synth::ui::SoundPackCredit& getCreditForTest(int index) { return *credits_[(size_t)index]; }
    juce::String getStatusTextForTest() const { return status_.getText(); }
    synth::ui::TextLinkButton& getRetryLinkForTest() noexcept { return retry_; }
    bool isRetryShownForTest() const { return retryFade_.isShown(); }
    juce::Viewport& getViewportForTest() noexcept { return viewport_; }

private:
    void soundPacksChanged() override;
    void refresh();
    void rebuildComponents(const std::vector<synth::soundpacks::SoundPackInstaller::Row>& rows);
    void layoutContent();
    void askToRemove(const juce::String& packId);

    synth::soundpacks::SoundPackInstaller& installer_;
    std::function<void(const juce::URL&)> urlOpener_ = [](const juce::URL& url) { url.launchInDefaultBrowser(); };
    std::function<void(const juce::String&, std::function<void()>)> confirmer_;

    juce::Label title_, intro_, status_, creditsHeading_;
    synth::ui::TextLinkButton retry_{"Retry"};
    synth::ui::FadeVisibility retryFade_{&retry_};
    juce::Viewport viewport_;
    juce::Component content_;
    synth::ui::FadeVisibility listFade_{&content_};
    synth::ui::FadeVisibility headingFade_{&creditsHeading_};
    synth::ui::ScrollIntoViewOnFocus followFocus_{viewport_};
    std::vector<std::unique_ptr<synth::ui::SoundPackRow>> rows_;
    std::vector<std::unique_ptr<synth::ui::SoundPackCredit>> credits_;
    juce::StringArray shownIds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundPacksSettingsTab)
};
