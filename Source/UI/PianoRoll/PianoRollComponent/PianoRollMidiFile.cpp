// PianoRollComponent — the "MIDI" header chip: import a Standard MIDI File into the OPEN clip, or
// export the open clip (notes plus CC lanes) as one. The file format itself is synth::MidiClipFile;
// this unit is only the user-facing flow around it. See docs/timeline/piano-roll-lanes.md#midi-files.

#include "PianoRollComponent.h"

#include "AppUndoManager.h"

namespace synth::ui {

juce::Rectangle<int> PianoRollComponent::getMidiButtonBounds() const noexcept { return midiButtonBounds_; }

void PianoRollComponent::showMidiMenu() {
    if (!isOpen())
        return;
    juce::PopupMenu menu;
    menu.addItem(1, "Import MIDI file into this clip...");
    menu.addItem(2, "Export this clip as a MIDI file...");
    juce::Component::SafePointer<PianoRollComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int result) {
        if (safe == nullptr)
            return;
        if (result == 1)
            safe->chooseMidiFileToImport();
        else if (result == 2)
            safe->chooseMidiFileToExport();
    });
}

// Async choosers only (launchAsync, never a modal loop). The chooser is a member because it must
// outlive this call; the SafePointer covers the roll going away while the dialog is up.
void PianoRollComponent::chooseMidiFileToImport() {
    midiFileChooser_ = std::make_unique<juce::FileChooser>("Import MIDI file", juce::File(), "*.mid;*.midi");
    juce::Component::SafePointer<PianoRollComponent> safe(this);
    midiFileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe](const juce::FileChooser& chooser) {
                                      if (safe == nullptr || chooser.getResult() == juce::File())
                                          return;
                                      const auto result = synth::MidiClipFile::importFromFile(chooser.getResult());
                                      if (result.ok)
                                          safe->importMidiIntoOpenClip(result);
                                  });
}

void PianoRollComponent::chooseMidiFileToExport() {
    const auto clipId = clipId_;
    const auto* clip = doc_ != nullptr ? doc_->getClip(clipId) : nullptr;
    if (clip == nullptr)
        return;
    const auto name = juce::File::createLegalFileName(clip->name.isNotEmpty() ? clip->name : juce::String("Clip"));
    midiFileChooser_ = std::make_unique<juce::FileChooser>("Export MIDI file", juce::File(name + ".mid"), "*.mid");
    juce::Component::SafePointer<PianoRollComponent> safe(this);
    midiFileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe, clipId](const juce::FileChooser& chooser) {
            if (safe == nullptr || safe->doc_ == nullptr || chooser.getResult() == juce::File())
                return;
            synth::MidiClipFile::exportClipToFile(*safe->doc_, clipId, chooser.getResult().withFileExtension("mid"));
        });
}

// Notes-only files import straight away. A file carrying CC data asks first — the CC stream can
// overwrite lanes the user drew, so it is their call — and the answer arrives later through
// applyMidiImportAnswer with the clip id captured NOW, never re-derived from whatever is open then.
bool PianoRollComponent::importMidiIntoOpenClip(const synth::MidiClipFile::ImportResult& result) {
    if (doc_ == nullptr || !clipId_.isValid() || !result.ok)
        return false;
    if (result.hasControllerData())
        promptMidiControllerImport(clipId_, result);
    else
        applyMidiImportAnswer(clipId_, result, false);
    return true;
}

// ASYNC, never a modal loop, and SafePointer-guarded — the same shape as promptExtendClipToFitNotes.
// The result is captured by value: the file may be gone by the time the user answers.
void PianoRollComponent::promptMidiControllerImport(synth::ClipId clipId,
                                                    const synth::MidiClipFile::ImportResult& result) {
    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle("Controller Data in MIDI File")
                       .withMessage("This file contains controller (CC) data. Import it into the clip's CC lanes too?")
                       .withButton("Import with controller data")
                       .withButton("Notes only");
    juce::Component::SafePointer<PianoRollComponent> safe(this);
    juce::AlertWindow::showAsync(options, [safe, clipId, result](int answer) {
        if (auto* self = safe.getComponent())
            self->applyMidiImportAnswer(clipId, result, answer == 1);
    });
}

void PianoRollComponent::applyMidiImportAnswer(synth::ClipId clipId, const synth::MidiClipFile::ImportResult& result,
                                               bool withControllers) {
    if (doc_ == nullptr || doc_->getClip(clipId) == nullptr)
        return;
    auto mutate = [this, clipId, &result, withControllers] {
        synth::MidiClipFile::importIntoClip(*doc_, clipId, result, withControllers);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();
}

} // namespace synth::ui
