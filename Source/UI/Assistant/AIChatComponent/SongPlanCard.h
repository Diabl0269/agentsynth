#pragma once

#include "SongCardBase.h"
#include "SongSession.h"
#include "SoundPacks/SoundPackInstaller.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ProgressStrip.h"
#include "UI/Layout/TextLinkButton.h"
#include <memory>
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

    // Test hooks.
    juce::String buildButtonText() const;
    juce::Component* sectionSegment(int index) const;
    int sectionSegmentCount() const;
    juce::String sectionSegmentLabelForTesting(int index) const;
    void chooseForTesting(int lengthIndex, int detailIndex);
    void pressBuildForTesting();
    juce::String soundsLineForTesting() const;
    bool soundsOfferShownForTesting() const;
    juce::Button& continueWithSynthsForTesting() { return continueLink; }
    juce::Component& soundsProgressForTesting() { return soundsProgress; }

private:
    class Strip;
    class Segment;
    class Chips;
    class Choice;

    int layoutFor(int width, bool apply) override;
    void applyPhase();
    juce::String detailsText() const;

    // The offer to download the core instruments first, shown while the plan names some and the pack is missing.
    void soundPacksChanged() override;
    bool offerNeeded() const;
    bool coreInstalled() const;
    bool downloadBusy() const;
    void startDownload();
    void refreshOffer();
    void continueWithSynths();

    SongSession& session;
    Actions actions;

    juce::Label headerLabel, titleLabel, metaLabel, specialLabel, noticeLabel, usesLabel;
    std::unique_ptr<Strip> strip;
    std::unique_ptr<Chips> chips;
    std::unique_ptr<Choice> lengthChoice, detailChoice;
    juce::TextButton buildButton;
    synth::ui::TextLinkButton cancelLink{"Cancel", juce::Justification::centredRight};
    synth::ui::TextLinkButton detailsLink{"Details"};
    juce::Label detailsLabel;
    bool detailsOpen = false;
    juce::Label lengthLabel, detailLabel;

    juce::SharedResourcePointer<synth::soundpacks::SoundPackInstaller> installer_;
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
