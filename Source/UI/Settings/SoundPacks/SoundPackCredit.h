#pragma once

// One pack's credit in Settings > Sound packs: who made the recordings, the licence, and links to read both.

#include "SoundPacks/SoundPackInstaller.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/TextLinkButton.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class SoundPackCredit : public juce::Component {
public:
    struct Actions {
        std::function<void(const juce::String& url)> openLicence;
        std::function<void(const juce::String& packId)> showSources;
    };

    SoundPackCredit(juce::String packId, Actions actions);

    void update(const synth::soundpacks::SoundPackInstaller::Row& row);
    /** The height this block needs at `width`, which wraps the credit text. */
    int heightForWidth(int width) const;
    const juce::String& getPackId() const noexcept { return packId_; }

    void resized() override;
    void lookAndFeelChanged() override;

    // ---- Test seams ----
    juce::String getCreditText() const { return credit_.getText(); }
    juce::String getLicenceText() const { return licence_.getText(); }
    TextLinkButton& getLicenceLink() noexcept { return licenceLink_; }
    TextLinkButton& getSourcesLink() noexcept { return sourcesLink_; }
    bool isLicenceLinkShown() const { return licenceFade_.isShown(); }
    bool isSourcesLinkShown() const { return sourcesFade_.isShown(); }

private:
    juce::String packId_;
    juce::String licenceUrl_;
    Actions actions_;
    juce::Label name_, credit_, licence_;
    TextLinkButton licenceLink_{"View licence"}, sourcesLink_{"Show sources"};
    FadeVisibility licenceFade_{&licenceLink_}, sourcesFade_{&sourcesLink_};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundPackCredit)
};

} // namespace synth::ui
