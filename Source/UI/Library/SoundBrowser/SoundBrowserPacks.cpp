// SoundBrowserPacks.cpp -- the packs you have not downloaded: their rows under Places ("Get more") and at the end of
// search results, the Download / Cancel button painted in the row, and the shared installer's progress. The download is
// the installer's own job, the same one Settings > Sound packs starts, so both places show the same bar.
#include "UI/Layout/SearchMatch.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserPackLayout.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

using Installer = synth::soundpacks::SoundPackInstaller;
using State = Installer::State;

constexpr float kGreyedAlpha = 0.55f;
constexpr int kBarHeight = 3;

bool isBusy(State state) {
    return state == State::Downloading || state == State::Verifying || state == State::Installing;
}

// Words a search can find a pack by: its name and the names of its instruments.
juce::String searchableText(const Installer::Row& pack) {
    juce::String text = pack.name;
    for (const auto& instrument : pack.instruments)
        text += " " + instrument.name;
    return text;
}

bool snapshotHasPlace(const synth::soundbrowser::SoundSnapshot& snap, const juce::String& id) {
    for (const auto& place : snap.places)
        if (place.id == id)
            return true;
    return false;
}

// What the row shows and does for the installer's current state of the pack.
void applyPackState(SoundBrowserComponent::Row& row, const Installer::Row& pack, bool inSnapshot) {
    row.bytes = pack.downloadBytes;
    row.busy = isBusy(pack.state) || (pack.installedVersion.isNotEmpty() && !inSnapshot);
    row.cancellable = pack.state == State::Downloading;
    row.progress = pack.state == State::Downloading ? pack.progress : (row.busy ? 1.0f : -1.0f);
    if (pack.state == State::Downloading)
        row.detail = pack.progress < 0.0f ? juce::String("Downloading")
                                          : juce::String(juce::roundToInt(pack.progress * 100.0f)) + "%";
    else if (row.busy)
        row.detail = pack.state == State::Verifying ? "Checking" : "Installing";
    else
        row.detail = synth::soundpacks::formatPackSize(pack.downloadBytes);
}

} // namespace

void SoundBrowserComponent::setPackInstaller(Installer* installer) {
    if (installer_ != nullptr)
        installer_->removeListener(this);
    installer_ = installer;
    if (installer_ != nullptr)
        installer_->addListener(this);
    packSignature_ = packSignature();
    rebuildRows();
    if (isVisible())
        loadPackListIfNeeded(); // the view may have turned visible before it had an installer to ask
}

// The list is fetched once, and again after a failed fetch; a load in flight or done is left alone.
void SoundBrowserComponent::loadPackListIfNeeded() {
    using Status = Installer::IndexStatus;
    if (installer_ != nullptr &&
        (installer_->indexStatus() == Status::NotLoaded || installer_->indexStatus() == Status::Failed))
        installer_->refreshIndex();
}

// Packs the hosted list offers that are not on this device. The list must have loaded: before that the view cannot
// tell a missing pack from one it does not know about. A pack the installer has finished stays here until the index
// lists its folder, so it never drops out of both lists between the two.
std::vector<Installer::Row> SoundBrowserComponent::offeredPacks(const synth::soundbrowser::SoundSnapshot& snap) const {
    std::vector<Installer::Row> offered;
    if (installer_ == nullptr || installer_->indexStatus() != Installer::IndexStatus::Loaded)
        return offered;
    for (auto& pack : installer_->rows())
        if (pack.inIndex && (pack.installedVersion.isEmpty() || !snapshotHasPlace(snap, pack.id)))
            offered.push_back(std::move(pack));
    return offered;
}

juce::String SoundBrowserComponent::packSignature() const {
    juce::String signature;
    for (const auto& pack : offeredPacks(*index_.snapshot()))
        signature += pack.id + ":" + juce::String((int)pack.state) + ";";
    return signature;
}

// Rows for the packs offered: under Places a "Get more" heading and one row each; in search results only the packs the
// query finds, by name or by an instrument's name.
void SoundBrowserComponent::appendOfferedPackRows(const synth::soundbrowser::SoundSnapshot& snap, bool places) {
    const auto offered = offeredPacks(snap);
    bool headed = false;
    for (const auto& pack : offered) {
        const bool byName = searchMatches(pack.name, query_);
        if (!places && !searchMatches(searchableText(pack), query_))
            continue;
        if (places && !headed) {
            rows_.push_back({RowKind::Hint, "Get more", {}, {}, {}, 0, std::nullopt});
            headed = true;
        }
        Row row;
        row.kind = RowKind::GetPack;
        row.text = pack.name;
        row.packId = pack.id;
        row.viaInstrument = !places && !byName;
        applyPackState(row, pack, snapshotHasPlace(snap, pack.id));
        rows_.push_back(std::move(row));
    }
}

void SoundBrowserComponent::soundPacksChanged() {
    const auto signature = packSignature();
    if (signature != packSignature_) {
        packSignature_ = signature;
        if (placeId_.isEmpty() || isSearchActive())
            rebuildRows();
        return;
    }
    refreshPackRows();
}

// Progress only: the rows stay, their bars and percentages move.
void SoundBrowserComponent::refreshPackRows() {
    if (installer_ == nullptr)
        return;
    const auto snap = index_.snapshot();
    bool any = false;
    for (const auto& pack : installer_->rows())
        for (auto& row : rows_)
            if (row.kind == RowKind::GetPack && row.packId == pack.id) {
                applyPackState(row, pack, snapshotHasPlace(*snap, pack.id));
                any = true;
            }
    if (any)
        repaint(listArea());
}

bool SoundBrowserComponent::toggleDownload(int index) {
    if (installer_ == nullptr || index < 0 || index >= (int)rows_.size() ||
        rows_[(size_t)index].kind != RowKind::GetPack)
        return false;
    const auto& row = rows_[(size_t)index];
    if (!row.busy)
        return installer_->install(row.packId);
    if (!row.cancellable)
        return false;
    installer_->cancel(row.packId);
    return true;
}

juce::Rectangle<int> SoundBrowserComponent::getPackButtonBounds(int index) const {
    if (index < 0 || index >= (int)rows_.size() || rows_[(size_t)index].kind != RowKind::GetPack)
        return {};
    const auto& row = rows_[(size_t)index];
    return row.busy && !row.cancellable ? juce::Rectangle<int>() : packButtonIn(getRowBounds(index));
}

bool SoundBrowserComponent::packButtonHit(int index, juce::Point<int> p) const {
    return getPackButtonBounds(index).contains(p);
}

juce::String SoundBrowserComponent::packTooltip(const Row& row) const {
    const auto size = synth::soundpacks::formatPackSize(row.bytes);
    if (row.busy)
        return "Downloading " + row.text + (row.cancellable ? ". Press Delete or click Cancel to stop." : ".");
    return "Download " + row.text + " (" + size + ")" + (row.viaInstrument ? " to use this sound" : "");
}

bool SoundBrowserComponent::activateSelectedAction() {
    if (selected_ < 0 || selected_ >= (int)rows_.size())
        return false;
    switch (rows_[(size_t)selected_].kind) {
    case RowKind::GetPack:
        if (!rows_[(size_t)selected_].busy)
            toggleDownload(selected_);
        return true;
    case RowKind::AddFolder:
        chooseFolderToAdd();
        return true;
    default:
        break;
    }
    return false;
}

// A greyed row: the name and the size (or the percentage), then the button; while the pack downloads a bar fills along
// the row's foot.
void SoundBrowserComponent::paintPackRow(juce::Graphics& g, const Row& row, int index,
                                         juce::Rectangle<int> area) const {
    const auto& c = synth::theme::themeOf(*this).colors;
    const auto button = packButtonIn(area);
    const bool showButton = !row.busy || row.cancellable;
    const int detailRight = showButton ? button.getX() - 8 : area.getRight() - kSoundRowSideMargin;
    const juce::Font detailFont(juce::FontOptions(12.0f));
    const int detailWidth = (int)std::ceil(juce::GlyphArrangement::getStringWidth(detailFont, row.detail)) + 2;
    g.setColour(c.textMuted);
    g.setFont(detailFont);
    g.drawText(row.detail, detailRight - detailWidth, area.getY(), detailWidth, area.getHeight(),
               juce::Justification::centredRight);
    g.setColour(c.textPrimary.withAlpha(kGreyedAlpha));
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.drawText(row.text, area.getX() + kSoundRowSideMargin, area.getY(),
               juce::jmax(0, detailRight - detailWidth - 8 - kSoundRowSideMargin), area.getHeight(),
               juce::Justification::centredLeft, true);
    if (showButton) {
        const auto& metrics = synth::theme::themeOf(*this).metrics;
        const auto pill = button.toFloat();
        g.setColour(c.surfaceHi);
        g.fillRoundedRectangle(pill, metrics.cornerRadiusSmall);
        g.setColour(index == packButtonHot_ ? c.accent : c.border);
        g.drawRoundedRectangle(pill.reduced(0.5f), metrics.cornerRadiusSmall, metrics.borderWidth);
        g.setColour(c.textPrimary);
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.drawText(row.busy ? "Cancel" : "Download", button, juce::Justification::centred);
    }
    if (row.busy) {
        const auto track = area.withTop(area.getBottom() - kBarHeight);
        g.setColour(c.border.withAlpha(0.6f));
        g.fillRect(track);
        g.setColour(c.accent);
        g.fillRect(track.withWidth(juce::roundToInt((float)area.getWidth() * juce::jlimit(0.0f, 1.0f, row.progress))));
    }
}

} // namespace synth::ui
