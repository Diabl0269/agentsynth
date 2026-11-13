#pragma once

// The Settings "Sound packs" tab: every downloadable and installed pack as a row with its state and buttons; opening a
// row shows who made the pack, its licence and every instrument. Which rows are open is remembered. Progress shown here
// is the shared installer's, so a download started from the Sampler's instrument picker appears here too.

#include "SoundBrowser/SoundFolderBook.h"
#include "SoundPacks/SoundPackInstaller.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/TextLinkButton.h"
#include "UI/Settings/SoundPacks/SoundFolderRow.h"
#include "UI/Settings/SoundPacks/SoundPackCredit.h"
#include "UI/Settings/SoundPacks/SoundPackRow.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class SoundPacksSettingsTab
    : public juce::Component
    , private synth::soundpacks::SoundPackInstaller::Listener
    , private synth::soundbrowser::SoundFolderBook::Listener {
public:
    /** The installer (and `folders`, when given) must outlive the tab. Fetches the pack list each time the tab is
     *  shown. With `folders` the tab also lists the user's own sound folders, each with a Remove button. With `props`
     *  the packs the user opened stay open the next time (a pack's id is what is saved). */
    explicit SoundPacksSettingsTab(synth::soundpacks::SoundPackInstaller& installer,
                                   synth::soundbrowser::SoundFolderBook* folders = nullptr,
                                   juce::ApplicationProperties* props = nullptr);
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
    synth::ui::SoundPackCredit& getCreditForTest(int index) { return rows_[(size_t)index]->getCredit(); }
    int getContentHeightForTest() const { return content_.getHeight(); }
    juce::Component& getContentForTest() noexcept { return content_; }
    juce::String getIntroTextForTest() const { return intro_.getText(); }
    juce::String getStatusTextForTest() const { return status_.getText(); }
    synth::ui::TextLinkButton& getRetryLinkForTest() noexcept { return retry_; }
    bool isRetryShownForTest() const { return retryFade_.isShown(); }
    juce::Viewport& getViewportForTest() noexcept { return viewport_; }
    int getFolderRowCountForTest() const { return (int)folderRows_.size(); }
    synth::ui::SoundFolderRow& getFolderRowForTest(int index) { return *folderRows_[(size_t)index]; }

private:
    void soundPacksChanged() override;
    void soundFoldersChanged() override;
    void rebuildFolderRows();
    void refresh();
    void rebuildComponents(const std::vector<synth::soundpacks::SoundPackInstaller::Row>& rows);
    void layoutContent();
    void askToRemove(const juce::String& packId);
    void saveOpenPacks();

    synth::soundpacks::SoundPackInstaller& installer_;
    synth::soundbrowser::SoundFolderBook* folders_;
    juce::ApplicationProperties* props_;
    juce::StringArray openPacks_; // the ids of the packs shown open
    std::function<void(const juce::URL&)> urlOpener_ = [](const juce::URL& url) { url.launchInDefaultBrowser(); };
    std::function<void(const juce::String&, std::function<void()>)> confirmer_;

    juce::Label title_, intro_, status_, foldersHeading_, foldersEmpty_;
    synth::ui::TextLinkButton retry_{"Retry"};
    synth::ui::FadeVisibility retryFade_{&retry_};
    juce::Viewport viewport_;
    juce::Component content_;
    synth::ui::FadeVisibility listFade_{&content_};
    synth::ui::ScrollIntoViewOnFocus followFocus_{viewport_};
    std::vector<std::unique_ptr<synth::ui::SoundPackRow>> rows_;
    std::vector<std::unique_ptr<synth::ui::SoundFolderRow>> folderRows_;
    juce::StringArray shownIds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundPacksSettingsTab)
};
