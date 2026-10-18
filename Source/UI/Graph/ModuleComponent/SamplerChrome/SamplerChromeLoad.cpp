// SamplerChromeLoad.cpp -- how the Sampler card loads: the Load Sample button and the card's file drop. The picker, its
// choosers and the one-step undo around an instrument load are in InstrumentPickerRow.cpp.
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerChrome.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kMinRecordingsForInstrument = 2;
constexpr float kHintHeight = 26.0f;
constexpr float kHintFontSize = 12.0f;
constexpr int kHintPadding = 24;

bool isSfzFile(const juce::File& file) { return file.hasFileExtension("sfz"); }

juce::StringArray recordingsIn(const juce::StringArray& files) {
    juce::StringArray recordings;
    for (const auto& path : files)
        if (SamplerModule::isSupportedAudioFile(juce::File(path)))
            recordings.add(path);
    return recordings;
}

} // namespace

// ---- Load Sample ----

void SamplerChrome::launchSampleChooser() {
    chooser_ = std::make_unique<juce::FileChooser>("Load Sample",
                                                   juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                   SamplerModule::getSupportedFormatWildcard());
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    // The chooser outlives this call; the weak reference keeps the callback a no-op once the card is gone (graph
    // rebuild, undo) while the dialog is open.
    juce::WeakReference<SamplerChrome> weak(this);
    chooser_->launchAsync(flags, [weak](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (weak == nullptr || file == juce::File{})
            return;
        weak->refreshSampleLabel(weak->sampler_.loadSampleFile(file) ? juce::String()
                                                                     : "Could not read " + file.getFileName());
    });
}

// ---- One undo step ----

bool SamplerChrome::loadUndoably(const std::function<bool(synth::sampler::InstrumentHost&)>& load) {
    return row_.loadUndoably(load);
}

// ---- File drop ----

bool SamplerChrome::isInterestedInDrop(const juce::StringArray& files) const {
    for (const auto& path : files)
        if (isSfzFile(juce::File(path)) || SamplerModule::isSupportedAudioFile(juce::File(path)))
            return true;
    return false;
}

void SamplerChrome::dragEntered(const juce::StringArray& files) {
    dragFileCount_ = recordingsIn(files).size();
    host_.card->repaint();
}

void SamplerChrome::dragLeft() {
    if (dragFileCount_ != 0)
        host_.card->repaint();
    dragFileCount_ = 0;
}

bool SamplerChrome::dropFiles(const juce::StringArray& files) {
    dragLeft();
    for (const auto& path : files) {
        const juce::File file(path);
        if (isSfzFile(file)) {
            row_.loadFromSfz(file);
            return true;
        }
    }
    auto recordings = recordingsIn(files);
    if (recordings.size() < kMinRecordingsForInstrument)
        return false;
    recordings.sort(true);
    row_.loadFromFiles(recordings);
    return true;
}

void SamplerChrome::paintDropHint(juce::Graphics& g, juce::Rectangle<int> cardBounds) const {
    if (dragFileCount_ < kMinRecordingsForInstrument)
        return;
    const auto& theme = synth::theme::themeOf(*host_.card);
    const juce::String text = juce::String(dragFileCount_) + " files: placed by the notes in their names";
    const juce::Font font{juce::FontOptions(kHintFontSize)};
    const float width = juce::GlyphArrangement::getStringWidth(font, text) + (float)kHintPadding;
    const auto pill = juce::Rectangle<float>(width, kHintHeight).withCentre(cardBounds.toFloat().getCentre());
    g.setColour(theme.colors.surfaceHi.withAlpha(0.95f));
    g.fillRoundedRectangle(pill, theme.metrics.cornerRadiusSmall);
    g.setColour(theme.colors.accent);
    g.drawRoundedRectangle(pill.reduced(0.5f), theme.metrics.cornerRadiusSmall, theme.metrics.borderWidth);
    g.setColour(theme.colors.textPrimary);
    g.setFont(font);
    g.drawText(text, pill, juce::Justification::centred, false);
}

} // namespace synth::ui
