#include "UI/Settings/SoundPacks/SoundFolderRow.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kPad = 8;
constexpr int kButtonW = 84;
constexpr int kButtonH = 26;
constexpr int kGap = 8;
constexpr int kNameH = 18;
constexpr int kLineH = 16;
} // namespace

SoundFolderRow::SoundFolderRow(const synth::soundbrowser::UserFolder& folder,
                               std::function<void(const juce::File&)> onRemove)
    : path_(folder.path) {
    setAccessible(true);
    setFocusContainerType(FocusContainerType::none);
    setTitle(folder.displayName() + ", " + folder.path.getFullPathName());
    for (auto* label : {&name_, &path_line_}) {
        addAndMakeVisible(*label);
        label->setMinimumHorizontalScale(1.0f);
        label->setInterceptsMouseClicks(false, false);
    }
    name_.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    path_line_.setFont(juce::Font(juce::FontOptions(11.5f)));
    name_.setText(folder.displayName(), juce::dontSendNotification);
    path_line_.setText(folder.path.getFullPathName() + (folder.path.isDirectory() ? "" : " - not found"),
                       juce::dontSendNotification);
    path_line_.setTooltip(folder.path.getFullPathName());

    addAndMakeVisible(remove_);
    remove_.setTitle("Remove " + folder.displayName() + " from your folders");
    remove_.setTooltip("Stop listing " + folder.path.getFullPathName() + ". The files stay where they are.");
    remove_.onClick = [this, onRemove = std::move(onRemove)] { onRemove(path_); };
    applyColours();
}

void SoundFolderRow::applyColours() {
    path_line_.setColour(juce::Label::textColourId, synth::theme::themeOf(*this).colors.textMuted);
}

void SoundFolderRow::lookAndFeelChanged() { applyColours(); }

void SoundFolderRow::resized() {
    auto area = getLocalBounds().reduced(kPad, kPad);
    auto button = area.removeFromRight(kButtonW);
    remove_.setBounds(button.withSizeKeepingCentre(kButtonW, kButtonH));
    area.removeFromRight(kGap);
    name_.setBounds(area.removeFromTop(kNameH));
    path_line_.setBounds(area.removeFromTop(kLineH));
}

void SoundFolderRow::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(theme.colors.surface);
    g.fillRoundedRectangle(bounds, theme.metrics.cornerRadiusSmall);
    g.setColour(theme.colors.border);
    g.drawRoundedRectangle(bounds, theme.metrics.cornerRadiusSmall, 1.0f);
}

} // namespace synth::ui
