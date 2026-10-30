// InstrumentPickerRow.cpp -- the picker list, its file choosers, the one-step undo around a load and the caption.
#include "UI/Graph/ModuleComponent/SamplerChrome/InstrumentPickerRow.h"
#include "AppUndoManager.h"
#include "Modules/ModuleFileStateKeys.h"
#include "Modules/SamplerModule.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerPackOffer.h"

namespace synth::ui {

namespace {
constexpr float kCaptionFontSize = 11.0f;
} // namespace

std::function<void(std::unique_ptr<ModMatrixPicker>)>& InstrumentPickerRow::pickerHookForTest() {
    static std::function<void(std::unique_ptr<ModMatrixPicker>)> hook;
    return hook;
}

InstrumentPickerRow::InstrumentPickerRow(const InstrumentChrome::Host& host, ModuleBase& module,
                                         synth::sampler::InstrumentHost& instruments, InstrumentChoiceOptions options)
    : host_(host)
    , module_(module)
    , instruments_(instruments)
    , options_(std::move(options)) {
    caption_.setJustificationType(juce::Justification::centredLeft);
    caption_.setFont(juce::Font(juce::FontOptions(kCaptionFontSize)));
    caption_.setMinimumHorizontalScale(0.7f);
    picker_.onClick = [this] { openPicker(); };
}

void InstrumentPickerRow::addPickerToCard() { host_.card->addChildComponent(picker_); }

void InstrumentPickerRow::addCaptionToCard() { host_.card->addChildComponent(caption_); }

InstrumentPickerRow::~InstrumentPickerRow() {
    for (juce::Component* part : {(juce::Component*)&picker_, (juce::Component*)&caption_})
        if (auto* parent = part->getParentComponent())
            parent->removeChildComponent(part);
}

int InstrumentPickerRow::layout(int y, int x, int pickerW, int captionW, bool apply) {
    const int captionY = y + synth::cardbody::kRowHeight + kCaptionGap;
    if (apply) {
        picker_.setBounds(x, y, pickerW, synth::cardbody::kRowHeight);
        caption_.setBounds(x, captionY, captionW, kCaptionHeight);
    }
    return captionY + kCaptionHeight;
}

// ---- Picker list ----

juce::String InstrumentPickerRow::currentPackKey() const {
    // getExtraState() builds a fresh object each call: keep the var alive, or `state` dangles.
    const auto extra = module_.getExtraState();
    if (auto* state = extra.getDynamicObject())
        return state->getProperty(synth::module_file_keys::kInstrumentPack).toString();
    return {};
}

void InstrumentPickerRow::openPicker() {
    auto options = options_;
    options.noKitLoaded = instruments_.getInstrumentName().isEmpty();
    auto choices = std::make_shared<InstrumentChoices>(collectInstrumentChoices(currentPackKey(), options));
    juce::WeakReference<InstrumentPickerRow> weak(this);
    auto popup = std::make_unique<ModMatrixPicker>("instrument", choices->items, 0, [weak, choices](int id) {
        if (weak != nullptr && id >= 1 && id <= (int)choices->choices.size())
            weak->chooseInstrument(choices->choices[(size_t)(id - 1)]);
    });
    popup->setAccessibleNames("Choose an instrument", "Search instruments");
    popup->setSearchPlaceholder("Search instruments...");
    attachPackOffer(*popup, choices, currentPackKey(), options);
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

void InstrumentPickerRow::chooseInstrument(const InstrumentChoice& choice) {
    switch (choice.kind) {
    case InstrumentChoice::Kind::Pack:
        loadUndoably([choice](synth::sampler::InstrumentHost& h) {
            return h.loadInstrumentFromPack(choice.packId, choice.instrumentId);
        });
        break;
    case InstrumentChoice::Kind::SfzFile:
        launchSfzChooser();
        break;
    case InstrumentChoice::Kind::Recordings:
        launchRecordingsChooser();
        break;
    case InstrumentChoice::Kind::SynthDrums:
        loadUndoably([](synth::sampler::InstrumentHost& h) {
            h.clearInstrument();
            return true;
        });
        break;
    }
}

void InstrumentPickerRow::launchSfzChooser() {
    chooser_ = std::make_unique<juce::FileChooser>(
        "Load an instrument file", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.sfz");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    juce::WeakReference<InstrumentPickerRow> weak(this);
    chooser_->launchAsync(flags, [weak](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (weak != nullptr && file != juce::File{})
            weak->loadFromSfz(file);
    });
}

void InstrumentPickerRow::launchRecordingsChooser() {
    chooser_ = std::make_unique<juce::FileChooser>("Load recordings",
                                                   juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                   SamplerModule::getSupportedFormatWildcard());
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                       juce::FileBrowserComponent::canSelectMultipleItems;
    juce::WeakReference<InstrumentPickerRow> weak(this);
    chooser_->launchAsync(flags, [weak](const juce::FileChooser& fc) {
        juce::StringArray paths;
        for (const auto& file : fc.getResults())
            paths.add(file.getFullPathName());
        if (weak != nullptr && !paths.isEmpty())
            weak->loadFromFiles(paths);
    });
}

void InstrumentPickerRow::loadFromFiles(const juce::StringArray& paths) {
    loadUndoably([paths](synth::sampler::InstrumentHost& h) { return h.loadInstrumentFromFiles(paths); });
}

void InstrumentPickerRow::loadFromSfz(const juce::File& sfz) {
    loadUndoably([sfz](synth::sampler::InstrumentHost& h) { return h.loadInstrumentFromSfz(sfz); });
}

// ---- One undo step ----

bool InstrumentPickerRow::loadUndoably(const std::function<bool(synth::sampler::InstrumentHost&)>& load) {
    const bool record = host_.undo != nullptr && host_.graph != nullptr;
    if (record)
        host_.undo->captureBeforeState(*host_.graph);
    const bool ok = load(instruments_);
    if (afterLoad)
        afterLoad(ok);
    if (record)
        host_.undo->pushSnapshotFromCapture(*host_.graph);
    reportLoad(ok);
    return ok;
}

// A failure or a warning shows on the caption line, the way a sample that could not be read shows on the file-name
// label; the tooltip holds every line.
void InstrumentPickerRow::reportLoad(bool ok) {
    loadMessages_ = instruments_.getInstrumentMessages();
    shownLoadSerial_ = instruments_.getInstrumentLoadSerial();
    if (!ok && loadMessages_.isEmpty())
        loadMessages_.add("Could not load that instrument");
    refresh();
}

// ---- Following the instrument ----

void InstrumentPickerRow::poll() {
    if (instruments_.getInstrumentLoadSerial() != shownLoadSerial_ ||
        instruments_.getInstrumentGeneration() != shownGeneration_)
        refresh();
}

void InstrumentPickerRow::refresh() {
    shownGeneration_ = instruments_.getInstrumentGeneration();
    // A load attempt this card has not shown yet (one that landed before the card was built included) brings its
    // messages; the card's own loads set them in reportLoad first.
    if (const int serial = instruments_.getInstrumentLoadSerial(); serial != shownLoadSerial_) {
        loadMessages_ = instruments_.getInstrumentMessages();
        shownLoadSerial_ = serial;
    }
    picker_.setInstrument(instruments_.getInstrumentName(), currentPackKey().isNotEmpty() ? "built in" : "your files");
    const juce::String idle = defaultCaption ? defaultCaption() : juce::String();
    if (!loadMessages_.isEmpty()) {
        const juce::String more =
            loadMessages_.size() > 1 ? " (+" + juce::String(loadMessages_.size() - 1) + " more)" : juce::String();
        caption_.setText(loadMessages_[0] + more, juce::dontSendNotification);
        caption_.setTooltip(loadMessages_.joinIntoString("\n"));
    } else {
        caption_.setText(idle, juce::dontSendNotification);
        caption_.setTooltip({});
    }
    host_.card->repaint();
    if (host_.kitChanged)
        host_.kitChanged();
}

} // namespace synth::ui
