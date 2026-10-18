#include "UI/Settings/SoundPacks/SoundPacksSettingsTab.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace {

constexpr int kPad = 10;
constexpr int kTitleH = 24;
constexpr int kIntroH = 34;
constexpr int kStatusH = 20;
constexpr int kRowGap = 6;
constexpr int kSectionGap = 12;
constexpr int kHeadingH = 22;
constexpr int kCreditGap = 12;
constexpr int kScrollbarW = 14;
constexpr int kRetryW = 50;

using Installer = synth::soundpacks::SoundPackInstaller;

void askWithAlert(const juce::String& message, std::function<void()> onYes) {
    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::QuestionIcon)
                             .withTitle("Remove sound pack")
                             .withMessage(message)
                             .withButton("Remove")
                             .withButton("Cancel");
    juce::AlertWindow::showAsync(options, [onYes = std::move(onYes)](int result) {
        if (result == 1 && onYes)
            onYes();
    });
}

} // namespace

SoundPacksSettingsTab::SoundPacksSettingsTab(Installer& installer)
    : installer_(installer)
    , confirmer_(askWithAlert) {
    setTitle("Sound packs");

    addAndMakeVisible(title_);
    title_.setText("Sound packs", juce::dontSendNotification);
    title_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));

    addAndMakeVisible(intro_);
    intro_.setText("Optional instruments for the Sampler. They download to this device when you ask for them. "
                   "Every pack's credit is listed below.",
                   juce::dontSendNotification);
    intro_.setJustificationType(juce::Justification::topLeft);
    intro_.setMinimumHorizontalScale(1.0f);

    addAndMakeVisible(status_);
    status_.setMinimumHorizontalScale(1.0f);
    addChildComponent(retry_);
    retry_.setTitle("Retry loading the sound pack list");
    retry_.setTooltip("Try to reach the list of sound packs again");
    retry_.onClick = [this] { installer_.refreshIndex(); };

    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setWantsKeyboardFocus(false);

    content_.addChildComponent(creditsHeading_);
    creditsHeading_.setText("Credits", juce::dontSendNotification);
    creditsHeading_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));

    lookAndFeelChanged();
    installer_.addListener(this);
    refresh();
}

SoundPacksSettingsTab::~SoundPacksSettingsTab() { installer_.removeListener(this); }

void SoundPacksSettingsTab::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

void SoundPacksSettingsTab::lookAndFeelChanged() {
    status_.setColour(juce::Label::textColourId, synth::theme::themeOf(*this).colors.textMuted);
}

void SoundPacksSettingsTab::visibilityChanged() {
    if (isShowing())
        installer_.refreshIndex();
}

void SoundPacksSettingsTab::resized() {
    auto bounds = getLocalBounds().reduced(kPad);
    title_.setBounds(bounds.removeFromTop(kTitleH));
    bounds.removeFromTop(2);
    intro_.setBounds(bounds.removeFromTop(kIntroH));
    bounds.removeFromTop(4);
    auto statusRow = bounds.removeFromTop(kStatusH);
    retry_.setBounds(statusRow.removeFromRight(kRetryW));
    status_.setBounds(statusRow);
    bounds.removeFromTop(4);
    viewport_.setBounds(bounds);
    layoutContent();
}

void SoundPacksSettingsTab::soundPacksChanged() { refresh(); }

void SoundPacksSettingsTab::refresh() {
    using Status = Installer::IndexStatus;
    const auto rows = installer_.rows();
    juce::String line;
    switch (installer_.indexStatus()) {
    case Status::Loading:
        line = "Checking for sound packs...";
        break;
    case Status::Failed:
        line = installer_.indexError();
        break;
    case Status::Loaded:
        if (rows.empty())
            line = "No sound packs are listed yet.";
        break;
    case Status::NotLoaded:
        break;
    }
    status_.setText(line, juce::dontSendNotification);
    retryFade_.setShown(installer_.indexStatus() == Status::Failed);
    rebuildComponents(rows);
}

// The row set changes when the list loads or a pack the list does not know is removed; then the rows are rebuilt and
// the whole list fades in. Otherwise each row just takes its new state.
void SoundPacksSettingsTab::rebuildComponents(const std::vector<Installer::Row>& rows) {
    juce::StringArray ids;
    for (const auto& row : rows)
        ids.add(row.id);
    const bool changed = ids != shownIds_;
    if (changed) {
        shownIds_ = ids;
        rows_.clear();
        credits_.clear();
        for (const auto& row : rows) {
            rows_.push_back(std::make_unique<synth::ui::SoundPackRow>(
                row.id, synth::ui::SoundPackRow::Actions{[this](const juce::String& id) { installer_.install(id); },
                                                         [this](const juce::String& id) { installer_.cancel(id); },
                                                         [this](const juce::String& id) { askToRemove(id); }}));
            credits_.push_back(std::make_unique<synth::ui::SoundPackCredit>(
                row.id,
                synth::ui::SoundPackCredit::Actions{[this](const juce::String& url) { urlOpener_(juce::URL(url)); },
                                                    [this](const juce::String& id) { installer_.showSources(id); }}));
            content_.addAndMakeVisible(*rows_.back());
            content_.addAndMakeVisible(*credits_.back());
        }
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        rows_[i]->update(rows[i]);
        credits_[i]->update(rows[i]);
    }
    layoutContent();
    if (changed) {
        listFade_.snapTo(false);
        listFade_.setShown(!rows.empty());
    }
    headingFade_.setShown(!rows.empty());
}

void SoundPacksSettingsTab::layoutContent() {
    const int width = juce::jmax(1, viewport_.getWidth() - kScrollbarW);
    int y = 0;
    for (auto& row : rows_) {
        row->setBounds(0, y, width, synth::ui::SoundPackRow::kHeight);
        y += synth::ui::SoundPackRow::kHeight + kRowGap;
    }
    y += kSectionGap;
    creditsHeading_.setBounds(0, y, width, kHeadingH);
    y += kHeadingH + kRowGap;
    for (auto& credit : credits_) {
        const int height = credit->heightForWidth(width);
        credit->setBounds(0, y, width, height);
        y += height + kCreditGap;
    }
    content_.setSize(width, juce::jmax(y, 1));
}

void SoundPacksSettingsTab::askToRemove(const juce::String& packId) {
    const auto row = installer_.row(packId);
    if (!row)
        return;
    juce::Component::SafePointer<SoundPacksSettingsTab> safe(this);
    confirmer_("Remove " + row->name +
                   " from this device? You can download it again later. Projects that use its "
                   "instruments will not find them until it is installed again.",
               [safe, packId] {
                   if (safe != nullptr)
                       safe->installer_.remove(packId);
               });
}
