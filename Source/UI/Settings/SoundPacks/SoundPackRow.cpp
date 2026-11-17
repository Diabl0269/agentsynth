#include "UI/Settings/SoundPacks/SoundPackRow.h"
#include "UI/Layout/TextFit.h"
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
constexpr int kDisclosureSize = 24; // the chevron's hit area, at the row's left
constexpr int kContentLeft = 32;    // where the name and the open details start
constexpr int kOpenGap = 4;         // between the closed band and the open details
constexpr int kNameH = 18;
constexpr int kLineH = 16;
constexpr float kNameSize = 13.0f;
constexpr float kLineSize = 11.5f;
constexpr int kLicenceGap = 10;  // between the state and the licence
constexpr int kMinLicenceW = 70; // the licence keeps at least this much; the state is cut first below it

juce::String detailText(const synth::soundpacks::SoundPackInstaller::Row& row) {
    juce::StringArray parts;
    const juce::String list = soundPackInstrumentList(row);
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

// `text` cut with a trailing ... so it is no wider than `width`.
juce::String fitToWidth(const juce::Font& font, const juce::String& text, int width) {
    juce::String cut = text;
    while (cut.isNotEmpty() && juce::GlyphArrangement::getStringWidthInt(font, cut + "...") > width)
        cut = cut.dropLastCharacters(1);
    return cut.trimEnd() + "...";
}

void setUpLabel(juce::Label& label, float size, bool bold) {
    label.setFont(juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)));
    label.setMinimumHorizontalScale(1.0f);
    label.setInterceptsMouseClicks(false, false);
}

} // namespace

SoundPackRow::SoundPackRow(juce::String packId, Actions actions)
    : packId_(std::move(packId))
    , actions_(std::move(actions))
    , credit_(packId_, SoundPackCredit::Actions{actions_.showSources}) {
    setAccessible(true);
    setFocusContainerType(FocusContainerType::none);
    for (auto* label : {&name_, &detail_, &status_})
        addAndMakeVisible(*label);
    addChildComponent(licence_);
    addChildComponent(openDetail_);
    setUpLabel(name_, kNameSize, true);
    setUpLabel(detail_, kLineSize, false);
    setUpLabel(openDetail_, kLineSize, false);
    detail_.setComponentID("packDetail:" + packId_); // test mode hovers it to read the tooltip popup
    name_.setComponentID("packName:" + packId_);     // test mode clicks it to open the row
    // The name and the line under it open and close the row like the chevron (as every Settings disclosure's label
    // does); the chevron stays the one Tab stop. The line also shows the full list as a tooltip when it is cut.
    for (auto* label : {&name_, &detail_, &openDetail_}) {
        label->setInterceptsMouseClicks(true, false);
        label->setMouseCursor(juce::MouseCursor::PointingHandCursor);
        label->addMouseListener(this, false);
    }
    setUpLabel(status_, kLineSize, false);
    setUpLabel(licence_, kLineSize, false);
    licence_.setJustificationType(juce::Justification::centredRight);
    licence_.setComponentID("packLicence:" + packId_); // test mode reads the licence of each closed row
    addChildComponent(licenceLink_);
    licenceLink_.setComponentID("packLicenceLink:" + packId_);
    licenceLink_.onClick = [this] {
        if (actions_.openLicence)
            actions_.openLicence(licenceUrl_);
    };
    for (auto* button : {&download_, &update_, &remove_, &cancel_})
        addChildComponent(*button);
    addChildComponent(progress_);
    addAndMakeVisible(disclosure_);
    disclosure_.setComponentID("packDisclosure:" + packId_); // test mode clicks it to open the row
    disclosure_.onOpenRequested = [this](bool open) { setExpanded(open); };
    addChildComponent(credit_);
    // The row's height follows the details' fade (they stay clipped to the row), so the owner lays the list out
    // again on every frame.
    creditFade_.onFrame = [this] {
        if (onHeightChanged)
            onHeightChanged();
    };

    download_.onClick = [this] { actions_.install(packId_); };
    update_.onClick = [this] { actions_.install(packId_); };
    cancel_.onClick = [this] { actions_.cancel(packId_); };
    remove_.onClick = [this] { actions_.remove(packId_); };
    applyColours();
}

void SoundPackRow::applyColours() {
    const auto& theme = synth::theme::themeOf(*this);
    detail_.setColour(juce::Label::textColourId, theme.colors.textMuted);
    openDetail_.setColour(juce::Label::textColourId, theme.colors.textMuted);
    licence_.setColour(juce::Label::textColourId, theme.colors.textMuted);
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
    disclosure_.setPackName(row.name);
    credit_.update(row);
    detail_.setText(detailText(row), juce::dontSendNotification);
    openDetail_.setText(row.downloadBytes > 0 ? formatPackSize(row.downloadBytes) : juce::String(),
                        juce::dontSendNotification);
    licenceUrl_ = row.licenceUrl;
    licenceText_ = row.licence.isNotEmpty() ? "Licence: " + row.licence : juce::String("Licence not stated");
    licence_.setText(licenceText_, juce::dontSendNotification);
    // A pack with a licence address shows its licence as the link; one without shows it as plain text.
    licenceLinkFade_.setShown(row.licenceUrl.isNotEmpty());
    licenceFade_.setShown(row.licenceUrl.isEmpty());
    licenceLink_.setTitle("View the licence for " + row.name);
    licenceLink_.setTooltip("Open the " + row.name + " licence page (" + row.licence + ") in your browser");
    setDescription(detail_.getText() + ". " + licenceText_);
    status_.setText(statusText(row), juce::dontSendNotification);
    if (!stateLine_.isEmpty())
        layoutStateLine(stateLine_);
    fitDetail();
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
    const int width = detail_.getBorderSize().subtractedFrom(detail_.getLocalBounds()).getWidth();
    detail_.setTooltip(textIsCut(detail_.getFont(), detail_.getText(), width) ? detail_.getText() : juce::String());
}

void SoundPackRow::layoutStateLine(juce::Rectangle<int> line) {
    stateLine_ = line;
    // The state keeps its words; the licence takes the rest at the right end, so it never moves when the state text
    // changes (a download's percentage), and is cut with an ellipsis when the row is narrow.
    const int stateW = juce::GlyphArrangement::getStringWidthInt(status_.getFont(), status_.getText()) +
                       status_.getBorderSize().getLeftAndRight() + 2;
    const int licenceMax = juce::jmax(kMinLicenceW, line.getWidth() - stateW - kLicenceGap);
    const bool asLink = licenceLinkFade_.isShown();
    const auto font = asLink ? textLinkFont() : licence_.getFont();
    const int pad = asLink ? 2 * kTextLinkPadX : licence_.getBorderSize().getLeftAndRight() + 2;
    const bool fits = juce::GlyphArrangement::getStringWidthInt(font, licenceText_) + pad <= licenceMax;
    const auto fitted = fits ? licenceText_ : fitToWidth(font, licenceText_, licenceMax - pad);
    const int width = juce::jmin(licenceMax, juce::GlyphArrangement::getStringWidthInt(font, fitted) + pad);
    const juce::Rectangle<int> box(line.getRight() - width, line.getY(), width, line.getHeight());
    licenceLink_.setButtonText(fitted);
    licenceLink_.setBounds(box);
    licence_.setText(fitted, juce::dontSendNotification);
    licence_.setBounds(box);
    licence_.setTooltip(fitted == licenceText_ ? juce::String() : licenceText_);
    status_.setBounds(line.getX(), line.getY(), juce::jmax(0, box.getX() - kLicenceGap - line.getX()),
                      line.getHeight());
}

int SoundPackRow::heightForWidth(int width) const {
    const float open = creditFade_.progress();
    if (open <= 0.0f)
        return kHeight;
    const int details = kOpenGap + credit_.heightForWidth(juce::jmax(1, width - kContentLeft - kPad)) + kPad;
    return kHeight + juce::roundToInt((float)details * open);
}

void SoundPackRow::setExpanded(bool expanded, bool animate) {
    if (expanded == isExpanded())
        return;
    disclosure_.setOpen(expanded, animate);
    // Open, the full list is in the details, so the cut line fades to just the size where it stood.
    if (animate) {
        creditFade_.setShown(expanded);
        detailFade_.setShown(!expanded);
        openDetailFade_.setShown(expanded);
    } else {
        creditFade_.snapTo(expanded);
        detailFade_.snapTo(!expanded);
        openDetailFade_.snapTo(expanded);
    }
    if (onExpandedChanged)
        onExpandedChanged();
}

void SoundPackRow::resized() {
    auto top = getLocalBounds().removeFromTop(kHeight);
    disclosure_.setBounds(kPad - 4, kPad - 3, kDisclosureSize, kDisclosureSize);
    auto area = top.reduced(kPad, kPad);
    area.removeFromLeft(kContentLeft - kPad);
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
    openDetail_.setBounds(detail_.getBounds());
    layoutStateLine(area.removeFromTop(kLineH));
    fitDetail();

    const int creditW = juce::jmax(1, getWidth() - kContentLeft - kPad);
    credit_.setBounds(kContentLeft, kHeight + kOpenGap, creditW, credit_.heightForWidth(creditW));
}

void SoundPackRow::mouseEnter(const juce::MouseEvent& e) {
    if (isTitlePart(e.eventComponent))
        disclosure_.setTitleHovered(true);
}

void SoundPackRow::mouseExit(const juce::MouseEvent& e) {
    if (isTitlePart(e.eventComponent))
        disclosure_.setTitleHovered(false);
}

void SoundPackRow::mouseUp(const juce::MouseEvent& e) {
    // A plain click (not a drag, not the right button) that ends on the part it started on.
    if (!isTitlePart(e.eventComponent) || !e.mouseWasClicked() || e.mods.isPopupMenu() ||
        !e.eventComponent->getLocalBounds().contains(e.getPosition()))
        return;
    setExpanded(!isExpanded());
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
