#include "UI/Settings/SoundPacks/SoundPackCredit.h"
#include "SoundPacks/SoundPackSourceList.h"
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

SoundPackCredit::SoundPackCredit(juce::String packId)
    : packId_(std::move(packId)) {
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

    addChildComponent(sourcesClip_);
    sourcesClip_.setInterceptsMouseClicks(false, false);
    sourcesClip_.addAndMakeVisible(sourcesList_);
    sourcesList_.setMinimumHorizontalScale(1.0f);
    sourcesList_.setInterceptsMouseClicks(false, false);
    sourcesList_.setFont(creditFont());
    sourcesList_.setJustificationType(juce::Justification::topLeft);
    sourcesList_.setComponentID("packSources:" + packId_); // test mode reads whether and what it lists
    sourcesListFade_.onFrame = [this] {
        resized();
        if (onHeightChanged)
            onHeightChanged();
    };

    addChildComponent(sourcesLink_);
    sourcesLink_.setComponentID("packSourcesLink:" + packId_); // test mode clicks it
    sourcesLink_.onClick = [this] { setSourcesOpen(!sourcesOpen_); };
    refreshSourcesLink();
    lookAndFeelChanged();
}

void SoundPackCredit::lookAndFeelChanged() {
    const auto& theme = synth::theme::themeOf(*this);
    credit_.setColour(juce::Label::textColourId, theme.colors.textMuted);
    instruments_.setColour(juce::Label::textColourId, theme.colors.textMuted);
    sourcesList_.setColour(juce::Label::textColourId, theme.colors.textMuted);
}

void SoundPackCredit::loadSourcesList() {
    const auto text = sourcesFile_ != juce::File() ? sourcesFile_.loadFileAsString() : juce::String();
    sourcesList_.setText(synth::soundpacks::soundPackSourceLines(text).joinIntoString("\n"),
                         juce::dontSendNotification);
}

void SoundPackCredit::refreshSourcesLink() {
    sourcesLink_.setButtonText(sourcesOpen_ ? "Hide sources" : "Show sources");
    sourcesLink_.setTitle((sourcesOpen_ ? "Hide the sources for " : "Show the sources for ") + name_);
    sourcesLink_.setTooltip(sourcesOpen_ ? "Hide the list of recordings"
                                         : "List the recordings in this pack and where each came from");
    sourcesLink_.setSize(sourcesLink_.getIdealWidth(), sourcesLink_.getHeight());
}

void SoundPackCredit::setSourcesOpen(bool open, bool animate) {
    if (open && sourcesFile_ == juce::File())
        open = false;
    if (open == sourcesOpen_ && sourcesListFade_.isShown() == open)
        return;
    sourcesOpen_ = open;
    if (open)
        loadSourcesList();
    refreshSourcesLink();
    if (animate)
        sourcesListFade_.setShown(open);
    else
        sourcesListFade_.snapTo(open);
    resized();
    // A fade that runs reports each frame itself; one that lands at once reports here.
    if (!sourcesListFade_.isFading() && onHeightChanged)
        onHeightChanged();
}

void SoundPackCredit::update(const synth::soundpacks::SoundPackInstaller::Row& row) {
    credit_.setText(row.credit.isNotEmpty() ? row.credit : juce::String("No credit line was provided for this pack."),
                    juce::dontSendNotification);
    const auto list = soundPackInstrumentList(row);
    instruments_.setText(list.isNotEmpty() ? "Instruments: " + list : juce::String(), juce::dontSendNotification);
    setTitle("Credit for " + row.name + ". " + credit_.getText());
    name_ = row.name;

    const bool fileChanged = row.sources != sourcesFile_;
    sourcesFile_ = row.sources;
    sourcesFade_.setShown(sourcesFile_ != juce::File());
    if (sourcesFile_ == juce::File() && (sourcesOpen_ || sourcesListFade_.isShown()))
        setSourcesOpen(false, false);
    else if (sourcesOpen_ && fileChanged)
        loadSourcesList();
    refreshSourcesLink();
    if (sourcesOpen_ && fileChanged)
        resized();
}

// The room the sources list takes now: its gap and wrapped height, times how far it has faded in.
int SoundPackCredit::sourcesListExtra(int width) const {
    const int listH = wrappedHeight(sourcesList_, sourcesList_.getText(), width);
    if (listH <= 0)
        return 0;
    return (int)std::lround((kGap + listH) * sourcesListFade_.progress());
}

int SoundPackCredit::heightForWidth(int width) const {
    int h = wrappedHeight(credit_, credit_.getText(), width) + kGap + kLinkH;
    const int instrumentsH = wrappedHeight(instruments_, instruments_.getText(), width);
    if (instrumentsH > 0)
        h += kGap + instrumentsH;
    h += sourcesListExtra(width);
    return h;
}

void SoundPackCredit::resized() {
    auto area = getLocalBounds();
    credit_.setBounds(area.removeFromTop(wrappedHeight(credit_, credit_.getText(), area.getWidth())));
    area.removeFromTop(kGap);
    auto links = area.removeFromTop(kLinkH);
    sourcesLink_.setBounds(links.removeFromLeft(sourcesLink_.getIdealWidth()));
    // The list stands at its full height right under the link; the instruments slide down by the faded amount.
    // The list keeps its full wrapped height (a shorter label would re-wrap), inside a holder that is only as tall as
    // the faded amount, so the list is uncovered from the top while the instruments slide down below it.
    const int listH = wrappedHeight(sourcesList_, sourcesList_.getText(), area.getWidth());
    const int extra = sourcesListExtra(area.getWidth());
    sourcesClip_.setBounds(area.getX(), area.getY() + kGap, area.getWidth(), juce::jmax(0, extra - kGap));
    sourcesList_.setBounds(0, 0, area.getWidth(), listH);
    area.removeFromTop(extra);
    area.removeFromTop(kGap);
    instruments_.setBounds(area);
}

} // namespace synth::ui
