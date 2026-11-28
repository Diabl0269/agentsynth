#pragma once

#include "SongCardBase.h"
#include "SongSession.h"
#include "SoundPacks/SoundPackInstaller.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FoldSectionHeader.h"
#include "UI/Layout/ProgressStrip.h"
#include "UI/Layout/StopButton.h"
#include "UI/Layout/TextLinkButton.h"
#include <memory>
#include <optional>
#include <vector>

namespace synth {

// The card a song request first gets: what would be built and what it costs, with nothing spent yet (see
// docs/ai/song-steps.md). It is a view of a SongSession; the chat owns the session and calls refresh() when it changes.
class SongPlanCard
    : public SongCardBase
    , private synth::soundpacks::SoundPackInstaller::Listener {
public:
    struct Actions {
        std::function<void()> onBuild;
        std::function<void(int lengthIndex, int detailIndex)> onReplan;
        std::function<void()> onCancel;
    };

    SongPlanCard(SongSession& session, Actions actions);
    ~SongPlanCard() override;

    /** Re-reads the session: the plan, the choices, the price, the phase and the notice. */
    void refresh();

    void parentHierarchyChanged() override;
    void lookAndFeelChanged() override;

    // Test hooks.
    juce::String buildButtonText() const;
    juce::Component* sectionSegment(int index) const;
    int sectionSegmentCount() const;
    juce::String sectionSegmentLabelForTesting(int index) const;
    void chooseForTesting(int lengthIndex, int detailIndex);
    void pressBuildForTesting();
    juce::String soundsLineForTesting() const;
    /** The key-change line under the meta line; empty (and hidden) when the key never changes. */
    juce::String keyLineForTesting() const { return keyLabel.isVisible() ? keyLabel.getText() : juce::String(); }
    /** The melody line under the key line; empty (and hidden) when no melody was involved. */
    juce::String melodyLineForTesting() const {
        return melodyLabel.isVisible() ? melodyLabel.getText() : juce::String();
    }
    bool soundsOfferShownForTesting() const;
    juce::String titleTextForTesting() const { return titleLabel.getText(); }
    juce::String metaTextForTesting() const { return metaLabel.getText(); }
    juce::String sectionsCaptionForTesting() const { return sectionsLabel.getText(); }
    juce::String sectionsTooltipForTesting() const;
    juce::Component& titleLabelForTesting() { return titleLabel; }
    juce::Component& metaLabelForTesting() { return metaLabel; }
    juce::Component& detailsFoldForTesting() { return detailsLink; }
    juce::Button& cancelLinkForTesting() { return cancelLink; }
    juce::Button& continueWithSynthsForTesting() { return continueLink; }
    juce::Component& soundsProgressForTesting() { return soundsProgress; }

private:
    class Strip;
    class Segment;
    class Chips;
    class Choice;

    int layoutFor(int width, bool apply) override;
    void applyBuildColours();
    void applyPhase();
    juce::String detailsText() const;

    // The offer to download the sound packs first, shown while the plan names sounds from a pack that is missing.
    void soundPacksChanged() override;
    bool offerNeeded() const;
    std::vector<juce::String> planPacks() const;
    std::vector<juce::String> neededPacks() const;
    bool packsInstalled() const;
    juce::String packsPhrase(const std::vector<juce::String>& packs) const;
    void installAll(const std::vector<juce::String>& packs);
    bool downloadBusy() const;
    void startDownload();
    void refreshOffer();
    void continueWithSynths();

    using Installer = synth::soundpacks::SoundPackInstaller;
    std::optional<Installer::Row> rowOf(const juce::String& id) const;
    // While alive, rowOf() reads one scan taken at construction instead of scanning per call.
    struct RowsScope {
        explicit RowsScope(const SongPlanCard& c)
            : card(c)
            , owns(!c.snapshot_) {
            if (owns)
                c.snapshot_ = c.installer_->rows();
        }
        ~RowsScope() {
            if (owns)
                card.snapshot_.reset();
        }
        const SongPlanCard& card;
        bool owns;
    };

    SongSession& session;
    Actions actions;

    juce::Label titleLabel, metaLabel, keyLabel, melodyLabel, sectionsLabel, specialLabel, noticeLabel, usesLabel;
    std::unique_ptr<Strip> strip;
    std::unique_ptr<Chips> chips;
    std::unique_ptr<Choice> lengthChoice, detailChoice;
    juce::TextButton buildButton;
    synth::ui::StopButton cancelLink{"Cancel"};
    synth::ui::FoldSectionHeader detailsLink{"Details"};
    juce::Label detailsLabel;
    bool detailsOpen = false;
    juce::Label lengthLabel, detailLabel;

    juce::SharedResourcePointer<synth::soundpacks::SoundPackInstaller> installer_;
    mutable std::optional<std::vector<Installer::Row>> snapshot_;
    juce::Label soundsLabel;
    synth::ui::ProgressStrip soundsProgress;
    synth::ui::TextLinkButton continueLink{"Continue with synths"};
    synth::ui::FadeVisibility soundsFade_{&soundsLabel, &continueLink};
    synth::ui::FadeVisibility soundsBarFade_{&soundsProgress};
    bool awaitingIndex_ = false; // Download first was pressed before the pack list arrived
    bool downloadFailed_ = false;

    std::unique_ptr<synth::ui::FadeVisibility> controlsFade_;
    synth::ui::FadeVisibility detailsFade_{&detailsLabel};
    synth::ui::FadeVisibility noticeFade_{&noticeLabel};
    SongSession::Phase shownPhase;
};

} // namespace synth
