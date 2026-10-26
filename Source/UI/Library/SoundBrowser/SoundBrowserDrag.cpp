// SoundBrowserDrag.cpp -- a Sound row dragged out of the Sounds view: the payload, the drag image and the app-wide
// "a sound is being dragged" state that the drop targets outline themselves from.
#include "UI/Layout/DragCursor.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundDragPayload.h"
#include "UI/Library/SoundBrowser/SoundDragState.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kChipFontSize = 13.0f;
constexpr int kChipHeight = 26;
constexpr int kChipPadding = 12;
constexpr int kChipMaxWidth = 240;
constexpr float kChipScale = 2.0f;
} // namespace

// A small chip with the sound's name, drawn at twice the size so it stays sharp on a high-density screen.
juce::ScaledImage SoundBrowserComponent::makeSoundDragImage(const juce::String& name) const {
    const auto& theme = synth::theme::themeOf(*this);
    const juce::Font font{juce::FontOptions(kChipFontSize)};
    const int width = juce::jmin(kChipMaxWidth,
                                 (int)std::ceil(juce::GlyphArrangement::getStringWidth(font, name)) + 2 * kChipPadding);
    juce::Image image(juce::Image::ARGB, juce::roundToInt((float)width * kChipScale),
                      juce::roundToInt((float)kChipHeight * kChipScale), true);
    juce::Graphics g(image);
    g.addTransform(juce::AffineTransform::scale(kChipScale));
    const auto chip = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)kChipHeight);
    g.setColour(theme.colors.surfaceHi.withAlpha(0.95f));
    g.fillRoundedRectangle(chip, theme.metrics.cornerRadiusSmall);
    g.setColour(theme.colors.accent);
    g.drawRoundedRectangle(chip.reduced(0.5f), theme.metrics.cornerRadiusSmall, theme.metrics.borderWidth);
    g.setColour(theme.colors.textPrimary);
    g.setFont(font);
    g.drawText(name, chip.reduced((float)kChipPadding, 0.0f), juce::Justification::centredLeft, true);
    return juce::ScaledImage(image, (double)kChipScale);
}

bool SoundBrowserComponent::beginSoundDrag(int index) {
    if (index < 0 || index >= (int)rows_.size() || rows_[(size_t)index].kind != RowKind::Sound)
        return false;
    const auto& row = rows_[(size_t)index];
    const juce::File file(row.entry->path);
    // The container is the window's (MainComponent): it paints the image over every view and flies it back to the row
    // on its own when the drop is not taken.
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        container->startDragging(makeSoundPayload(file.getFullPathName()), this, makeSoundDragImage(row.text));
    soundDragging_ = true;
    showDragCursor(*this);
    SoundDragState::begin(file, row.entry->kind == synth::soundbrowser::SoundKind::Loop
                                    ? loopTargetFor(*row.entry)
                                    : synth::soundbrowser::LoopTarget{});
    return true;
}

void SoundBrowserComponent::mouseDrag(const juce::MouseEvent& e) {
    if (pressedSound_ < 0 || soundDragging_ || e.getDistanceFromDragStart() < kDragStartThresholdPx)
        return;
    const int row = pressedSound_;
    pressedSound_ = -1;
    if (!beginShapeDrag(row))
        beginSoundDrag(row);
}

// The container's drag image listens to the same release and drops after this returns; clearing the outlines first
// does not change what the drop sees.
void SoundBrowserComponent::mouseUp(const juce::MouseEvent&) {
    pressedSound_ = -1;
    if (!soundDragging_)
        return;
    soundDragging_ = false;
    endDragCursor(*this);
    SoundDragState::end();
}

} // namespace synth::ui
