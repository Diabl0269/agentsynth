#pragma once

// The details of one pack, shown inside its row in Settings > Sound packs when the row is open: who made the
// recordings, a link that lists the sources in place (under the link, fading and gliding open) and every instrument.
// (The licence is not here: it stays on the closed row, see SoundPackRow.)

#include "SoundPacks/SoundPackInstaller.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/TextLinkButton.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** The pack's instrument names, comma separated (the row's detail line and the open details both use it). */
juce::String soundPackInstrumentList(const synth::soundpacks::SoundPackInstaller::Row& row);

class SoundPackCredit : public juce::Component {
public:
    explicit SoundPackCredit(juce::String packId);

    void update(const synth::soundpacks::SoundPackInstaller::Row& row);
    /** The height this block needs at `width`, which wraps the credit text and the instrument list. */
    int heightForWidth(int width) const;
    const juce::String& getPackId() const noexcept { return packId_; }

    void resized() override;
    void lookAndFeelChanged() override;

    /** Called on each frame of the sources list's fade and after a toggle that lands at once: the height changed. */
    std::function<void()> onHeightChanged;

    /** Shows or hides the sources list (and turns the link's words to match); it fades and glides unless `animate`
     *  is false or the app is not animating. */
    void setSourcesOpen(bool open, bool animate = true);
    bool isSourcesOpen() const noexcept { return sourcesOpen_; }

    // ---- Test seams ----
    juce::String getCreditText() const { return credit_.getText(); }
    juce::String getInstrumentsText() const { return instruments_.getText(); }
    TextLinkButton& getSourcesLink() noexcept { return sourcesLink_; }
    bool isSourcesLinkShown() const { return sourcesFade_.isShown(); }
    bool isSourcesListShown() const { return sourcesListFade_.isShown(); }
    int getInstrumentsY() const { return instruments_.getY(); }
    juce::String getSourcesListText() const { return sourcesList_.getText(); }

private:
    juce::String packId_;
    juce::String name_;
    juce::File sourcesFile_;
    bool sourcesOpen_ = false;
    juce::Label credit_, instruments_, sourcesList_;
    juce::Component sourcesClip_; // holds the list at full height and is only as tall as the faded amount
    TextLinkButton sourcesLink_{"Show sources"};
    FadeVisibility sourcesFade_{&sourcesLink_};
    FadeVisibility sourcesListFade_{&sourcesClip_};

    int sourcesListExtra(int width) const;
    void loadSourcesList();
    void refreshSourcesLink();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundPackCredit)
};

} // namespace synth::ui
