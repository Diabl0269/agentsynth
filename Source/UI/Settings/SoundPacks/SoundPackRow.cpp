#include "UI/Settings/SoundPacks/SoundPackRow.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

using synth::soundpacks::formatPackSize;
using State = synth::soundpacks::SoundPackInstaller::State;

namespace {

constexpr int kPad = 8;
constexpr int kButtonW = 84;
constexpr int kButtonH = 26;
constexpr int kGap = 8;
constexpr int kBarH = 6;
constexpr int kNameH = 18;
constexpr int kLineH = 16;
constexpr float kNameSize = 13.0f;
constexpr float kLineSize = 11.5f;

juce::String instrumentList(const synth::soundpacks::SoundPackInstaller::Row& row) {
    juce::StringArray names;
    for (const auto& instrument : row.instruments)
        names.add(instrument.name);
    return names.joinIntoString(", ");
}

juce::String detailText(const synth::soundpacks::SoundPackInstaller::Row& row) {
    juce::StringArray parts;
    const juce::String list = instrumentList(row);
    if (list.isNotEmpty())
        parts.add(list);
    if (row.downloadBytes > 0)
        parts.add(formatPackSize(row.downloadBytes));
    return parts.joinIntoString(" - ");
}

juce::String percentText(float progress) {
    return progress < 0.0f ? juce::String() : " " + juce::String(juce::roundToInt(progress * 100.0f)) + "%";
}

juce::String statusText(const synth::soundpacks::SoundPackInstaller::Row& row) {
    switch (row.state) {
    case State::Downloading:
        return "Downloading" + percentText(row.progress);
    case State::Verifying:
        return "Checking the download";
    case State::Installing:
        return "Installing";
    case State::UpdateAvailable:
        return "Update available: version " + row.latestVersion;
    case State::Installed:
        return "Installed, version " + row.installedVersion;
    case State::NotInstalled:
        break;
    }
    return row.error.isNotEmpty() ? row.error : juce::String("Not installed");
}

juce::String spokenState(const synth::soundpacks::SoundPackInstaller::Row& row) {
    switch (row.state) {
    case State::Downloading:
        return "downloading" + (row.progress < 0.0f
                                    ? juce::String()
                                    : ", " + juce::String(juce::roundToInt(row.progress * 100.0f)) + " percent");
    case State::Verifying:
        return "checking the download";
    case State::Installing:
        return "installing";
    case State::UpdateAvailable:
        return "update available";
    case State::Installed:
        return "installed";
    case State::NotInstalled:
        break;
    }
    return "not installed";
}

void setUpLabel(juce::Label& label, float size, bool bold) {
    label.setFont(juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)));
    label.setMinimumHorizontalScale(1.0f);
    label.setInterceptsMouseClicks(false, false);
}

} // namespace

SoundPackRow::SoundPackRow(juce::String packId, Actions actions)
    : packId_(std::move(packId))
    , actions_(std::move(actions)) {
    setAccessible(true);
    setFocusContainerType(FocusContainerType::none);
    for (auto* label : {&name_, &detail_, &status_})
        addAndMakeVisible(*label);
    setUpLabel(name_, kNameSize, true);
    setUpLabel(detail_, kLineSize, false);
    detail_.setInterceptsMouseClicks(true, false); // hovering it shows the full list when the line is elided
    setUpLabel(status_, kLineSize, false);
    for (auto* button : {&download_, &update_, &remove_, &cancel_})
        addChildComponent(*button);
    addChildComponent(progress_);

    download_.onClick = [this] { actions_.install(packId_); };
    update_.onClick = [this] { actions_.install(packId_); };
    cancel_.onClick = [this] { actions_.cancel(packId_); };
    remove_.onClick = [this] { actions_.remove(packId_); };
    applyColours();
}

void SoundPackRow::applyColours() {
    const auto& theme = synth::theme::themeOf(*this);
    detail_.setColour(juce::Label::textColourId, theme.colors.textMuted);
    status_.setColour(juce::Label::textColourId, hasError_ ? theme.colors.error : theme.colors.textMuted);
}

void SoundPackRow::lookAndFeelChanged() { applyColours(); }

bool SoundPackRow::isButtonShown(const juce::Button& button) const {
    if (&button == &download_)
        return downloadFade_.isShown();
    if (&button == &update_)
        return updateFade_.isShown();
    if (&button == &remove_)
        return removeFade_.isShown();
    return cancelFade_.isShown();
}

void SoundPackRow::showControls(const synth::soundpacks::SoundPackInstaller::Row& row) {
    const bool busy =
        row.state == State::Downloading || row.state == State::Verifying || row.state == State::Installing;
    downloadFade_.setShown(row.state == State::NotInstalled && row.inIndex);
    updateFade_.setShown(row.state == State::UpdateAvailable);
    removeFade_.setShown((row.state == State::Installed || row.state == State::UpdateAvailable) && row.removable);
    cancelFade_.setShown(busy);
    cancel_.setEnabled(row.state != State::Installing);
}

void SoundPackRow::update(const synth::soundpacks::SoundPackInstaller::Row& row) {
    hasError_ = row.state == State::NotInstalled && row.error.isNotEmpty();
    name_.setText(row.name, juce::dontSendNotification);
    detail_.setText(detailText(row), juce::dontSendNotification);
    setDescription(detail_.getText());
    fitDetail();
    status_.setText(statusText(row), juce::dontSendNotification);
    applyColours();

    const juce::String size = row.downloadBytes > 0 ? " (" + formatPackSize(row.downloadBytes) + ")" : juce::String();
    download_.setTitle("Download " + row.name);
    download_.setTooltip("Download " + row.name + size + " to this device");
    update_.setTitle("Update " + row.name);
    update_.setTooltip("Update " + row.name + " to version " + row.latestVersion);
    remove_.setTitle("Remove " + row.name);
    remove_.setTooltip("Remove " + row.name + " from this device. You are asked to confirm first.");
    cancel_.setTitle("Cancel the " + row.name + " download");
    cancel_.setTooltip("Stop downloading " + row.name + " and discard what was downloaded");
    progress_.setSubject("Downloading " + row.name);
    progress_.setFraction(row.state == State::Downloading ? juce::jmax(0.0f, row.progress) : 1.0f);

    setTitle(row.name + ", " + spokenState(row) +
             (size.isNotEmpty() ? ", " + formatPackSize(row.downloadBytes) : juce::String()) +
             (row.installedVersion.isNotEmpty() ? ", version " + row.installedVersion : juce::String()));
    showControls(row);
}

void SoundPackRow::fitDetail() {
    const int needed = juce::GlyphArrangement::getStringWidthInt(detail_.getFont(), detail_.getText());
    const bool fits = needed <= detail_.getWidth();
    detail_.setTooltip(fits ? juce::String() : detail_.getText());
}

void SoundPackRow::resized() {
    auto area = getLocalBounds().reduced(kPad, kPad);
    auto buttons = area.removeFromRight(kButtonW * 2 + kGap);
    const auto vCentre = [&buttons](int height) { return buttons.getCentreY() - height / 2; };
    const int primaryX = buttons.getRight() - kButtonW;
    const int secondaryX = primaryX - kGap - kButtonW;
    for (auto* b : {&download_, &remove_, &cancel_})
        b->setBounds(primaryX, vCentre(kButtonH), kButtonW, kButtonH);
    update_.setBounds(secondaryX, vCentre(kButtonH), kButtonW, kButtonH);
    progress_.setBounds(secondaryX, vCentre(kBarH), kButtonW, kBarH);

    area.removeFromRight(kGap);
    name_.setBounds(area.removeFromTop(kNameH));
    detail_.setBounds(area.removeFromTop(kLineH));
    status_.setBounds(area.removeFromTop(kLineH));
    fitDetail();
}

void SoundPackRow::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(theme.colors.surface);
    g.fillRoundedRectangle(bounds, theme.metrics.cornerRadiusSmall);
    g.setColour(theme.colors.border);
    g.drawRoundedRectangle(bounds, theme.metrics.cornerRadiusSmall, 1.0f);
}

} // namespace synth::ui
