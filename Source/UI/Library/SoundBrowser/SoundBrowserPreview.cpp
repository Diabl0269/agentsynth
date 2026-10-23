// SoundBrowserPreview.cpp -- what the Sounds view asks of the preview player: play the selected sound, stop it, Space,
// the footer's controls, and keeping the footer's waveform on the selected sound.
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"

namespace synth::ui {

using synth::soundbrowser::SoundKind;

void SoundBrowserComponent::setupPreview() {
    footer_.onStop = [this] { stopPreview(); };
    footer_.onAutoPlayChanged = [this](bool) {
        if (onPreviewSettingsChanged)
            onPreviewSettingsChanged();
    };
    footer_.onGainChanged = [this](float db) {
        if (previewPlayer_ != nullptr)
            previewPlayer_->setGainDb(db);
        if (onPreviewSettingsChanged)
            onPreviewSettingsChanged();
    };
    addAndMakeVisible(footer_);
}

void SoundBrowserComponent::setPreviewPlayer(synth::soundbrowser::SoundPreviewPlayer* player) {
    previewPlayer_ = player;
    footer_.setPlayer(player);
    if (player != nullptr)
        player->setGainDb(footer_.getGainDb());
}

void SoundBrowserComponent::setPreviewSettings(bool autoPlay, float gainDb) {
    footer_.setAutoPlay(autoPlay);
    footer_.setGainDb(gainDb);
    if (previewPlayer_ != nullptr)
        previewPlayer_->setGainDb(footer_.getGainDb());
}

// A loop starts on the next bar when the song plays; instruments are not audio files and stay silent.
bool SoundBrowserComponent::playSelected() {
    if (previewPlayer_ == nullptr || selected_ < 0 || selected_ >= (int)rows_.size() ||
        rows_[(size_t)selected_].kind != RowKind::Sound)
        return false;
    const auto& entry = *rows_[(size_t)selected_].entry;
    const juce::File file(entry.path);
    if (entry.kind == SoundKind::Instrument || !file.existsAsFile())
        return false;
    lastRequestId_ = previewPlayer_->play(file, entry.kind == SoundKind::Loop);
    footer_.followRequest(lastRequestId_);
    return true;
}

void SoundBrowserComponent::stopPreview() {
    if (previewPlayer_ != nullptr)
        previewPlayer_->stop();
}

bool SoundBrowserComponent::togglePreview() {
    if (previewPlayer_ == nullptr)
        return false;
    if (previewPlayer_->isPlaying()) {
        previewPlayer_->stop();
        return true;
    }
    return playSelected();
}

void SoundBrowserComponent::syncFooterToSelection() {
    std::optional<synth::soundbrowser::SoundEntry> entry;
    if (selected_ >= 0 && selected_ < (int)rows_.size() && rows_[(size_t)selected_].kind == RowKind::Sound)
        entry = rows_[(size_t)selected_].entry;
    footer_.showSound(entry);
}

// Showing the view loads the hosted pack list (for "Get more") when it is not loaded and looks again at the user's
// folders; hiding it stops the preview.
void SoundBrowserComponent::visibilityChanged() {
    if (!isVisible()) {
        stopPreview();
        return;
    }
    if (!isShowing())
        return;
    checkFoldersOnReturn();
    using Status = synth::soundpacks::SoundPackInstaller::IndexStatus;
    if (installer_ != nullptr &&
        (installer_->indexStatus() == Status::NotLoaded || installer_->indexStatus() == Status::Failed))
        installer_->refreshIndex();
}

} // namespace synth::ui
