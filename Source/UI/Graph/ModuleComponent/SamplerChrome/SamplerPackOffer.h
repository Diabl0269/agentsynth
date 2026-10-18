#pragma once

// The strip under the Sampler's instrument list while no sound pack is installed: offers the core instruments,
// downloads them in place with progress, and hands the list back when they are there.

#include "SoundPacks/SoundPackInstaller.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ProgressStrip.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

class ModMatrixPicker;
struct InstrumentChoices;
struct InstrumentChoiceOptions;

class SamplerPackOffer
    : public juce::Component
    , private synth::soundpacks::SoundPackInstaller::Listener {
public:
    static constexpr int kHeight = 54;

    /** Uses the app's shared installer and fetches the pack list if it has not been fetched. */
    SamplerPackOffer();
    ~SamplerPackOffer() override;

    /** Runs once, the moment the core pack is installed: the instrument list can fill now. */
    std::function<void()> onInstalled;
    /** Runs once after the offer has faded away: the owner may remove it. Not called from inside the fade. */
    std::function<void()> onFinished;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void lookAndFeelChanged() override;
    void parentHierarchyChanged() override;

    /** True while no installed pack lists an instrument: the only time the offer is made. */
    static bool isNeeded();

    // ---- Test seams ----
    juce::TextButton& getDownloadButton() noexcept { return download_; }
    juce::TextButton& getCancelButton() noexcept { return cancel_; }
    ProgressStrip& getProgress() noexcept { return progress_; }
    juce::String getMessageText() const { return message_.getText(); }
    bool isOfferShown() const { return panelFade_.isShown(); }
    bool isDownloadShown() const { return downloadFade_.isShown(); }
    bool isBusyShown() const { return busyFade_.isShown(); }

private:
    void soundPacksChanged() override;
    void refresh();
    void finish();
    void applyColours();
    void onDownloadClicked();

    juce::SharedResourcePointer<synth::soundpacks::SoundPackInstaller> installer_;
    juce::Component panel_;
    juce::Label message_;
    juce::TextButton download_{"Download"}, cancel_{"Cancel"};
    ProgressStrip progress_;
    FadeVisibility panelFade_{&panel_};
    FadeVisibility downloadFade_{&download_};
    FadeVisibility busyFade_{&cancel_, &progress_};
    bool finished_ = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE(SamplerPackOffer)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerPackOffer)
};

/** Adds the offer under `picker`'s list when no pack is installed. When the pack lands the list is rebuilt into
 *  `choices` (which the picker's pick handler reads) with the same `options` and the offer is removed. Does nothing
 *  when packs exist. */
void attachPackOffer(ModMatrixPicker& picker, std::shared_ptr<InstrumentChoices> choices,
                     const juce::String& currentPack, const InstrumentChoiceOptions& options);

} // namespace synth::ui
