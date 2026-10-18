// SamplerChromeLoad.cpp -- how the Sampler card loads: the Load Sample button, the instrument picker and its file
// choosers, the one-step undo around an instrument load, and the card's file drop.
#include "AppUndoManager.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerChrome.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerInstrumentChoices.h"
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

std::function<void(std::unique_ptr<ModMatrixPicker>)>& SamplerChrome::pickerHookForTest() {
    static std::function<void(std::unique_ptr<ModMatrixPicker>)> hook;
    return hook;
}

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

// ---- Instrument picker ----

void SamplerChrome::openPicker() {
    auto choices = std::make_shared<InstrumentChoices>(collectInstrumentChoices(currentPackKey()));
    juce::WeakReference<SamplerChrome> weak(this);
    auto popup = std::make_unique<ModMatrixPicker>("instrument", choices->items, 0, [weak, choices](int id) {
        if (weak != nullptr && id >= 1 && id <= (int)choices->choices.size())
            weak->chooseInstrument(choices->choices[(size_t)(id - 1)]);
    });
    popup->setAccessibleNames("Choose an instrument", "Search instruments");
    popup->setSearchPlaceholder("Search instruments...");
    popup->onClosed = [weak] {
        if (weak != nullptr)
            weak->picker_.setListOpen(false);
    };
    picker_.setListOpen(true);
    if (auto& hook = pickerHookForTest()) {
        hook(std::move(popup));
        return;
    }
    juce::CallOutBox::launchAsynchronously(std::move(popup), picker_.getScreenBounds(), nullptr);
}

void SamplerChrome::chooseInstrument(const InstrumentChoice& choice) {
    switch (choice.kind) {
    case InstrumentChoice::Kind::Pack:
        loadUndoably(
            [choice](SamplerModule& s) { return s.loadInstrumentFromPack(choice.packId, choice.instrumentId); });
        break;
    case InstrumentChoice::Kind::SfzFile:
        launchSfzChooser();
        break;
    case InstrumentChoice::Kind::Recordings:
        launchRecordingsChooser();
        break;
    }
}

void SamplerChrome::launchSfzChooser() {
    chooser_ = std::make_unique<juce::FileChooser>(
        "Load an instrument file", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.sfz");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    juce::WeakReference<SamplerChrome> weak(this);
    chooser_->launchAsync(flags, [weak](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (weak != nullptr && file != juce::File{})
            weak->loadUndoably([file](SamplerModule& s) { return s.loadInstrumentFromSfz(file); });
    });
}

void SamplerChrome::launchRecordingsChooser() {
    chooser_ = std::make_unique<juce::FileChooser>("Load recordings",
                                                   juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                   SamplerModule::getSupportedFormatWildcard());
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                       juce::FileBrowserComponent::canSelectMultipleItems;
    juce::WeakReference<SamplerChrome> weak(this);
    chooser_->launchAsync(flags, [weak](const juce::FileChooser& fc) {
        juce::StringArray paths;
        for (const auto& file : fc.getResults())
            paths.add(file.getFullPathName());
        if (weak != nullptr && !paths.isEmpty())
            weak->loadFromFiles(paths);
    });
}

void SamplerChrome::loadFromFiles(const juce::StringArray& paths) {
    loadUndoably([paths](SamplerModule& s) { return s.loadInstrumentFromFiles(paths); });
}

// ---- One undo step ----

bool SamplerChrome::loadUndoably(const std::function<bool(SamplerModule&)>& load) {
    const bool record = host_.undo != nullptr && host_.graph != nullptr;
    if (record)
        host_.undo->captureBeforeState(*host_.graph);
    const bool ok = load(sampler_);
    if (record)
        host_.undo->pushSnapshotFromCapture(*host_.graph);
    reportLoad(ok);
    return ok;
}

// A failure or a warning shows on the caption line, the way a sample that could not be read shows on the file-name
// label; the tooltip holds every line.
void SamplerChrome::reportLoad(bool ok) {
    loadMessages_ = sampler_.getInstrumentMessages();
    shownLoadSerial_ = sampler_.getInstrumentLoadSerial();
    if (!ok && loadMessages_.isEmpty())
        loadMessages_.add("Could not load that instrument");
    refreshInstrument();
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
            loadUndoably([file](SamplerModule& s) { return s.loadInstrumentFromSfz(file); });
            return true;
        }
    }
    auto recordings = recordingsIn(files);
    if (recordings.size() < kMinRecordingsForInstrument)
        return false;
    recordings.sort(true);
    loadFromFiles(recordings);
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
