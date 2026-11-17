#include "UI/Settings/SoundPacks/SoundPackCredit.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kLinkH = 18;
constexpr int kGap = 2;
constexpr float kCreditSize = 11.5f;

juce::Font creditFont() { return juce::Font(juce::FontOptions(kCreditSize)); }

// The height a label needs to wrap `text` at `width` (its own side borders taken off).
int wrappedHeight(const juce::Label& label, const juce::String& text, int width) {
    if (text.isEmpty())
        return 0;
    juce::AttributedString attributed;
    attributed.append(text, creditFont());
    attributed.setWordWrap(juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout(attributed, (float)juce::jmax(1, width - label.getBorderSize().getLeftAndRight()));
    return (int)std::ceil(layout.getHeight()) + 2;
}

} // namespace

juce::String soundPackInstrumentList(const synth::soundpacks::SoundPackInstaller::Row& row) {
    juce::StringArray names;
    for (const auto& instrument : row.instruments)
        names.add(instrument.name);
    return names.joinIntoString(", ");
}

SoundPackCredit::SoundPackCredit(juce::String packId, Actions actions)
    : packId_(std::move(packId))
    , actions_(std::move(actions)) {
    setAccessible(true);
    for (auto* label : {&credit_, &instruments_}) {
        addAndMakeVisible(*label);
        label->setMinimumHorizontalScale(1.0f);
        label->setInterceptsMouseClicks(false, false);
    }
    credit_.setFont(creditFont());
    credit_.setJustificationType(juce::Justification::topLeft);
    instruments_.setFont(creditFont());
    instruments_.setJustificationType(juce::Justification::topLeft);

    addChildComponent(sourcesLink_);
    sourcesLink_.onClick = [this] {
        if (actions_.showSources)
            actions_.showSources(packId_);
    };
    lookAndFeelChanged();
}

void SoundPackCredit::lookAndFeelChanged() {
    const auto& theme = synth::theme::themeOf(*this);
    credit_.setColour(juce::Label::textColourId, theme.colors.textMuted);
    instruments_.setColour(juce::Label::textColourId, theme.colors.textMuted);
}

void SoundPackCredit::update(const synth::soundpacks::SoundPackInstaller::Row& row) {
    credit_.setText(row.credit.isNotEmpty() ? row.credit : juce::String("No credit line was provided for this pack."),
                    juce::dontSendNotification);
    const auto list = soundPackInstrumentList(row);
    instruments_.setText(list.isNotEmpty() ? "Instruments: " + list : juce::String(), juce::dontSendNotification);
    setTitle("Credit for " + row.name + ". " + credit_.getText());

    sourcesFade_.setShown(row.sources != juce::File());
    sourcesLink_.setTitle("Show the sources for " + row.name);
    sourcesLink_.setTooltip("Open the list of recordings and where each came from");
}

int SoundPackCredit::heightForWidth(int width) const {
    int h = wrappedHeight(credit_, credit_.getText(), width) + kGap + kLinkH;
    const int instrumentsH = wrappedHeight(instruments_, instruments_.getText(), width);
    if (instrumentsH > 0)
        h += kGap + instrumentsH;
    return h;
}

void SoundPackCredit::resized() {
    auto area = getLocalBounds();
    credit_.setBounds(area.removeFromTop(wrappedHeight(credit_, credit_.getText(), area.getWidth())));
    area.removeFromTop(kGap);
    auto links = area.removeFromTop(kLinkH);
    sourcesLink_.setBounds(links.removeFromLeft(sourcesLink_.getIdealWidth()));
    area.removeFromTop(kGap);
    instruments_.setBounds(area);
}

} // namespace synth::ui
