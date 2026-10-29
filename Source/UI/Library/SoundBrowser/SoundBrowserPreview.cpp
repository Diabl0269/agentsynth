// SoundBrowserPreview.cpp -- what the Sounds view asks of the preview player: play the selected sound, stop it, Space,
// the footer's controls, and keeping the footer's waveform on the selected sound.
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"

namespace synth::ui {

using synth::soundbrowser::SoundKind;

void SoundBrowserComponent::setupPreview() {
    footer_.onStop = [this] { stopPreview(); };
    footer_.onAutoPlayChanged = [this](bool) {
        if (onPreviewSettingsChanged)
            onPreviewSettingsChanged();
    };
    footer_.onMatchKeyChanged = [this](bool) {
        refreshMatchKeyHint();
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

void SoundBrowserComponent::setPreviewSettings(bool autoPlay, float gainDb, bool matchKey) {
    footer_.setAutoPlay(autoPlay);
    footer_.setMatchKey(matchKey);
    refreshMatchKeyHint();
    footer_.setGainDb(gainDb);
    if (previewPlayer_ != nullptr)
        previewPlayer_->setGainDb(footer_.getGainDb());
}

// A loop starts on the next bar when the song plays and is fitted to the song's tempo (and key, with Match key on);
// instruments are not audio files and stay silent.
bool SoundBrowserComponent::playSelected() {
    if (previewPlayer_ == nullptr || selected_ < 0 || selected_ >= (int)rows_.size() ||
        rows_[(size_t)selected_].kind != RowKind::Sound)
        return false;
    const auto& entry = *rows_[(size_t)selected_].entry;
    const juce::File file(entry.path);
    if (entry.kind == SoundKind::Instrument || !file.existsAsFile())
        return false;
    const bool loop = entry.kind == SoundKind::Loop;
    lastRequestId_ = loop ? previewPlayer_->play(file, true, loopTargetFor(entry)) : previewPlayer_->play(file, false);
    footer_.followRequest(lastRequestId_);
    return true;
}

// A loop of unknown tempo gets fileBpm 0, which the player leaves as recorded.
synth::soundbrowser::LoopTarget
SoundBrowserComponent::loopTargetFor(const synth::soundbrowser::SoundEntry& entry) const {
    const double songBpm = songTempoProvider ? songTempoProvider() : 0.0;
    return synth::soundbrowser::makeLoopTarget(entry.tempo, entry.key, songBpm, filters_.songKey(),
                                               footer_.isMatchKey());
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
    std::optional<synth::shapes::LibraryShape> shape;
    if (selected_ >= 0 && selected_ < (int)rows_.size()) {
        if (rows_[(size_t)selected_].kind == RowKind::Sound)
            entry = rows_[(size_t)selected_].entry;
        else if (rows_[(size_t)selected_].kind == RowKind::Shape)
            shape = rows_[(size_t)selected_].shape;
    }
    footer_.showSound(entry);
    footer_.showShape(shape);
    refreshMatchKeyHint();
}

void SoundBrowserComponent::addTestModeState(juce::DynamicObject& into) const {
    const bool sound =
        selected_ >= 0 && selected_ < (int)rows_.size() && rows_[(size_t)selected_].kind == RowKind::Sound;
    into.setProperty("selectedName", sound ? rows_[(size_t)selected_].text : juce::String());
    into.setProperty("selectedDetail", sound ? rows_[(size_t)selected_].detail : juce::String());
    into.setProperty("footerLine", sound ? soundFooterLine(*rows_[(size_t)selected_].entry) : juce::String());
}

juce::String SoundBrowserComponent::selectedMatchKeyText() const {
    if (selected_ < 0 || selected_ >= (int)rows_.size() || rows_[(size_t)selected_].kind != RowKind::Sound ||
        rows_[(size_t)selected_].entry->kind != SoundKind::Loop)
        return {};
    return matchKeyText(loopTargetFor(*rows_[(size_t)selected_].entry));
}

// The footer line, the switch's tooltip and its screen-reader description all read this one text.
void SoundBrowserComponent::refreshMatchKeyHint() {
    const auto text = selectedMatchKeyText();
    auto& toggle = footer_.getMatchKey();
    toggle.setTooltip(text.isNotEmpty() ? text : juce::String("Play loops in the song's key"));
    toggle.setDescription(text);
    repaint();
}

// Showing the view loads the hosted pack list (for "Get more") when it is not loaded and looks again at the user's
// folders; hiding it stops the preview. The list loads even before the window is on screen: a view restored at
// startup turns visible while its window is still hidden and gets no second call when the window appears.
void SoundBrowserComponent::visibilityChanged() {
    if (!isVisible()) {
        stopPreview();
        return;
    }
    loadPackListIfNeeded();
    if (isShowing())
        checkFoldersOnReturn();
}

} // namespace synth::ui
