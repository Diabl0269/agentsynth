#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerPackOffer.h"
#include "Modules/Sampler/SoundPackLibrary.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerInstrumentChoices.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

using synth::soundpacks::formatPackSize;
using Installer = synth::soundpacks::SoundPackInstaller;
using State = Installer::State;

namespace {

const char* const kCorePackId = "core";
constexpr int kPad = 6;
constexpr int kButtonW = 76;
constexpr int kButtonH = 24;
constexpr int kBarH = 6;
constexpr int kGap = 8;
constexpr float kMessageSize = 12.0f;

juce::String percent(float progress) {
    return progress < 0.0f ? juce::String() : " " + juce::String(juce::roundToInt(progress * 100.0f)) + "%";
}

} // namespace

SamplerPackOffer::SamplerPackOffer() {
    setTitle("Get the core instruments");
    setAccessible(true);
    addAndMakeVisible(panel_);
    panel_.addAndMakeVisible(message_);
    message_.setFont(juce::Font(juce::FontOptions(kMessageSize)));
    message_.setMinimumHorizontalScale(1.0f);
    message_.setJustificationType(juce::Justification::centredLeft);
    message_.setInterceptsMouseClicks(false, false);
    panel_.addChildComponent(download_);
    panel_.addChildComponent(cancel_);
    panel_.addChildComponent(progress_);

    download_.onClick = [this] { onDownloadClicked(); };
    cancel_.setTitle("Cancel the core instruments download");
    cancel_.setTooltip("Stop downloading the core instruments and discard what was downloaded");
    cancel_.onClick = [this] { installer_->cancel(kCorePackId); };
    progress_.setSubject("Downloading the core instruments");

    installer_->addListener(this);
    using Status = Installer::IndexStatus;
    if (installer_->indexStatus() == Status::NotLoaded || installer_->indexStatus() == Status::Failed)
        installer_->refreshIndex();
    refresh();
}

SamplerPackOffer::~SamplerPackOffer() { installer_->removeListener(this); }

bool SamplerPackOffer::isNeeded() { return synth::sampler::SoundPackLibrary::listInstruments().empty(); }

void SamplerPackOffer::onDownloadClicked() {
    if (installer_->indexStatus() == Installer::IndexStatus::Loaded && installer_->row(kCorePackId))
        installer_->install(kCorePackId);
    else
        installer_->refreshIndex();
}

void SamplerPackOffer::soundPacksChanged() { refresh(); }

// Maps the shared installer's state for the core pack onto the strip's one message, button and bar.
void SamplerPackOffer::refresh() {
    using Status = Installer::IndexStatus;
    const auto row = installer_->row(kCorePackId);
    const bool onList = row && row->inIndex;
    if (row && (row->state == State::Installed || row->state == State::UpdateAvailable)) {
        finish();
        return;
    }
    const bool busy =
        row && (row->state == State::Downloading || row->state == State::Verifying || row->state == State::Installing);
    const juce::String size = onList ? " (" + formatPackSize(row->downloadBytes) + ")" : juce::String();
    juce::String message = "Get the core instruments" + size;
    juce::String button = "Download";
    bool buttonOn = onList;
    if (busy) {
        message = row->state == State::Downloading ? "Downloading the core instruments" + percent(row->progress)
                                                   : juce::String("Installing the core instruments");
    } else if (row && row->error.isNotEmpty()) {
        message = row->error;
        button = "Try again";
    } else if (installer_->indexStatus() == Status::Failed && !onList) {
        message = installer_->indexError();
        button = "Retry";
        buttonOn = true;
    } else if (!onList) {
        message = "Checking for the core instruments...";
    }
    message_.setText(message, juce::dontSendNotification);
    download_.setButtonText(button);
    download_.setEnabled(buttonOn);
    download_.setTitle(button == "Download" ? juce::String("Download the core instruments") : button);
    download_.setTooltip(button == "Download" ? "Download the core instruments" + size + " to this device"
                                              : juce::String("Try again"));
    progress_.setFraction(busy && row->state == State::Downloading ? juce::jmax(0.0f, row->progress) : 1.0f);
    cancel_.setEnabled(busy && row->state != State::Installing);
    downloadFade_.setShown(!busy);
    busyFade_.setShown(busy);
    setTitle(message);
}

// The pack is in: let the list fill at once, fade the strip away, then tell the owner it can go.
void SamplerPackOffer::finish() {
    if (finished_)
        return;
    finished_ = true;
    if (onInstalled)
        onInstalled();
    juce::WeakReference<SamplerPackOffer> weak(this);
    const auto done = [weak] {
        if (weak != nullptr && weak->onFinished)
            weak->onFinished();
    };
    panelFade_.onHidden = [done] { juce::MessageManager::callAsync(done); };
    panelFade_.setShown(false);
    if (!panelFade_.isFading())
        juce::MessageManager::callAsync(done);
}

void SamplerPackOffer::resized() {
    panel_.setBounds(getLocalBounds());
    auto area = panel_.getLocalBounds().reduced(kPad, kPad);
    auto buttons = area.removeFromRight(kButtonW);
    const int y = buttons.getCentreY() - kButtonH / 2;
    download_.setBounds(buttons.getX(), y, kButtonW, kButtonH);
    cancel_.setBounds(buttons.getX(), y, kButtonW, kButtonH);
    area.removeFromRight(kGap);
    auto text = area;
    message_.setBounds(text.removeFromTop(text.getHeight() - kBarH - 2));
    progress_.setBounds(text.removeFromBottom(kBarH));
}

void SamplerPackOffer::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    g.setColour(theme.colors.border);
    g.drawHorizontalLine(0, 0.0f, (float)getWidth());
}

void SamplerPackOffer::applyColours() {
    message_.setColour(juce::Label::textColourId, synth::theme::themeOf(*this).colors.textMuted);
}

void SamplerPackOffer::lookAndFeelChanged() { applyColours(); }

void SamplerPackOffer::parentHierarchyChanged() { applyColours(); }

void attachPackOffer(ModMatrixPicker& picker, std::shared_ptr<InstrumentChoices> choices,
                     const juce::String& currentPack, const InstrumentChoiceOptions& options) {
    if (!SamplerPackOffer::isNeeded())
        return;
    auto offer = std::make_unique<SamplerPackOffer>();
    juce::Component::SafePointer<ModMatrixPicker> safePicker(&picker);
    offer->onInstalled = [safePicker, choices, currentPack, options] {
        if (safePicker == nullptr)
            return;
        *choices = collectInstrumentChoices(currentPack, options);
        safePicker->setItems(choices->items);
    };
    offer->onFinished = [safePicker] {
        if (safePicker != nullptr)
            safePicker->clearFooter();
    };
    picker.setFooter(std::move(offer), SamplerPackOffer::kHeight);
}

} // namespace synth::ui
