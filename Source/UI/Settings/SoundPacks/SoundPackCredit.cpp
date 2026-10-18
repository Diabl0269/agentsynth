#include "UI/Settings/SoundPacks/SoundPackCredit.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kNameH = 18;
constexpr int kLicenceH = 18;
constexpr int kLinkH = 18;
constexpr int kGap = 2;
constexpr float kCreditSize = 11.5f;

juce::Font creditFont() { return juce::Font(juce::FontOptions(kCreditSize)); }

} // namespace

SoundPackCredit::SoundPackCredit(juce::String packId, Actions actions)
    : packId_(std::move(packId))
    , actions_(std::move(actions)) {
    setAccessible(true);
    for (auto* label : {&name_, &credit_, &licence_}) {
        addAndMakeVisible(*label);
        label->setMinimumHorizontalScale(1.0f);
        label->setInterceptsMouseClicks(false, false);
    }
    name_.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    credit_.setFont(creditFont());
    credit_.setJustificationType(juce::Justification::topLeft);
    licence_.setFont(creditFont());

    addChildComponent(licenceLink_);
    addChildComponent(sourcesLink_);
    licenceLink_.onClick = [this] {
        if (actions_.openLicence)
            actions_.openLicence(licenceUrl_);
    };
    sourcesLink_.onClick = [this] {
        if (actions_.showSources)
            actions_.showSources(packId_);
    };
    lookAndFeelChanged();
}

void SoundPackCredit::lookAndFeelChanged() {
    const auto& theme = synth::theme::themeOf(*this);
    credit_.setColour(juce::Label::textColourId, theme.colors.textMuted);
    licence_.setColour(juce::Label::textColourId, theme.colors.textMuted);
}

void SoundPackCredit::update(const synth::soundpacks::SoundPackInstaller::Row& row) {
    licenceUrl_ = row.licenceUrl;
    name_.setText(row.name, juce::dontSendNotification);
    credit_.setText(row.credit.isNotEmpty() ? row.credit : juce::String("No credit line was provided for this pack."),
                    juce::dontSendNotification);
    licence_.setText(row.licence.isNotEmpty() ? "Licence: " + row.licence : juce::String("Licence not stated"),
                     juce::dontSendNotification);
    setTitle("Credit for " + row.name + ". " + credit_.getText() + " " + licence_.getText());

    licenceFade_.setShown(row.licenceUrl.isNotEmpty());
    licenceLink_.setTitle("View the licence for " + row.name);
    licenceLink_.setTooltip("Open the " + row.name + " licence in your browser");
    sourcesFade_.setShown(row.sources != juce::File());
    sourcesLink_.setTitle("Show the sources for " + row.name);
    sourcesLink_.setTooltip("Open the list of recordings and where each came from");
}

int SoundPackCredit::heightForWidth(int width) const {
    juce::AttributedString text;
    text.append(credit_.getText(), creditFont());
    text.setWordWrap(juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout(text, (float)juce::jmax(1, width));
    const int creditH = (int)std::ceil(layout.getHeight()) + 2;
    return kNameH + kGap + creditH + kGap + kLicenceH + kGap + kLinkH;
}

void SoundPackCredit::resized() {
    auto area = getLocalBounds();
    name_.setBounds(area.removeFromTop(kNameH));
    area.removeFromTop(kGap);
    const int creditH = juce::jmax(kLinkH, area.getHeight() - kGap - kLicenceH - kGap - kLinkH);
    credit_.setBounds(area.removeFromTop(creditH));
    area.removeFromTop(kGap);
    licence_.setBounds(area.removeFromTop(kLicenceH));
    area.removeFromTop(kGap);
    auto links = area.removeFromTop(kLinkH);
    licenceLink_.setBounds(links.removeFromLeft(90));
    links.removeFromLeft(8);
    sourcesLink_.setBounds(links.removeFromLeft(90));
}

} // namespace synth::ui
