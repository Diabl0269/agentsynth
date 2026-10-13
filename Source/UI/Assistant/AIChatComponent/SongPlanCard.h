#pragma once

#include "SongCardBase.h"
#include "SongSession.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/TextLinkButton.h"
#include <memory>
#include <vector>

namespace synth {

// The card a song request first gets: what would be built and what it costs, with nothing spent yet (see
// docs/ai/song-steps.md). It is a view of a SongSession; the chat owns the session and calls refresh() when it changes.
class SongPlanCard : public SongCardBase {
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
    void chooseForTesting(int lengthIndex, int detailIndex);
    void pressBuildForTesting();

private:
    class Strip;
    class Segment;
    class Chips;
    class Choice;

    int layoutFor(int width, bool apply) override;
    void applyPhase();
    juce::String detailsText() const;

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

    std::unique_ptr<synth::ui::FadeVisibility> controlsFade_;
    synth::ui::FadeVisibility detailsFade_{&detailsLabel};
    synth::ui::FadeVisibility noticeFade_{&noticeLabel};
    SongSession::Phase shownPhase;
};

} // namespace synth
