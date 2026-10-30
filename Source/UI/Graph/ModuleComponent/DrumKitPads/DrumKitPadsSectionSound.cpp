// DrumKitPadsSectionSound.cpp -- what a pad plays: the Sound row and pad menus, each choice as one undo step, a
// sound dropped on a pad, an instrument pad's pitch and Open, and the words the row and caption show for it.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/ModuleTitle.h"
#include "AudioEngine/SamplerMidiWiring.h"
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumKitPadsSection.h"
#include "UI/Layout/ContextMenuPlacement.h"

namespace synth::ui {

using synth::drums::PadKind;
using Kind = DrumPadSoundMenu::Choice::Kind;

void DrumKitPadsSection::wireSoundControls() {
    controls_.describeSound = [this](int index) { return describeSound(index); };
    controls_.onSoundMenu = [this] { openMenu(controls_.boundPad(), false, false); };
    controls_.onOpenInstrument = [this] { showInstrument(controls_.boundPad()); };
    controls_.onPitchStep = [this](int delta) {
        const int index = controls_.boundPad();
        setInstrumentPitchUndoably(index, kit_.pad(index).instrumentPitch + delta);
    };
    controls_.onPitchTyped = [this](const juce::String& text) {
        const int index = controls_.boundPad();
        if (text.containsAnyOf("0123456789"))
            setInstrumentPitchUndoably(index, text.getIntValue());
        controls_.refresh();
    };
    grid_.onContextMenu = [this](int index, bool keyboard) { openMenu(index, true, keyboard); };
    grid_.onFileDropped = [this](int index, const juce::File& file) { setPadFileUndoably(index, file); };
    grid_.onDropTargetChanged = [this] { refreshCaption(); };
}

// ---- Menus ----

void DrumKitPadsSection::openMenu(int index, bool padMenu, bool keyboard) {
    const auto instruments = DrumPadSoundMenu::instrumentsIn(host_.graph);
    auto menu = padMenu ? soundMenu_.buildPadMenu(kit_, index, instruments)
                        : soundMenu_.buildSoundMenu(kit_, index, instruments);
    if (showMenuForTest) {
        showMenuForTest(menu, index, padMenu);
        return;
    }
    std::optional<juce::Rectangle<int>> anchor;
    if (keyboard || !padMenu) {
        const auto& from = padMenu ? static_cast<juce::Component&>(grid_) : controls_.soundButton();
        const auto local = padMenu ? grid_.padBounds(index) : controls_.soundButton().getLocalBounds();
        anchor = from.localAreaToGlobal(local);
    }
    controls_.soundButton().setListOpen(!padMenu);
    juce::Component::SafePointer<juce::Component> card(host_.card);
    menu.showMenuAsync(contextMenuOptions(anchor), [this, card, index](int result) {
        if (card == nullptr)
            return;
        controls_.soundButton().setListOpen(false);
        applyMenuResult(index, result);
    });
}

void DrumKitPadsSection::applyMenuResult(int index, int result) { applyChoice(index, soundMenu_.choiceFor(result)); }

void DrumKitPadsSection::applyChoice(int index, const DrumPadSoundMenu::Choice& choice) {
    auto pad = kit_.pad(index);
    switch (choice.kind) {
    case Kind::None:
        return;
    case Kind::Play:
        grid_.pick(index, true);
        return;
    case Kind::ChooseFile:
        chooseFileFor(index);
        return;
    case Kind::File:
        setPadFileUndoably(index, juce::File(choice.file));
        return;
    case Kind::ToggleShown:
        setPadShown(index, !kit_.padShown(index));
        return;
    case Kind::ShowAll:
        showAllPads();
        return;
    case Kind::Reset:
        editPadUndoably([&] {
            kit_.resetPad(index);
            return true;
        });
        return;
    case Kind::BuiltIn:
    case Kind::KitSound:
        pad.kind = choice.kind == Kind::KitSound ? PadKind::Kit : PadKind::BuiltIn;
        pad.sound = choice.sound;
        pad.name = synth::drums::soundName(choice.sound);
        pad.file.clear();
        pad.instrumentUuid.clear();
        editPadUndoably([&] { return kit_.setPad(index, pad); });
        return;
    case Kind::Instrument:
        // The capture writes every node's uuid, so the target has one by the time the pad stores it.
        editPadUndoably([&] {
            auto* node = host_.graph != nullptr ? host_.graph->getNodeForId(choice.node) : nullptr;
            if (node == nullptr)
                return false;
            pad.kind = PadKind::Instrument;
            pad.instrumentUuid = synth::AIStateMapper::ensureNodeUuid(node);
            pad.name = synth::moduleTitle(*node).substring(0, 64);
            pad.file.clear();
            return kit_.setPad(index, pad);
        });
        return;
    }
}

// ---- Edits ----

// One undo step per edit, and the instrument links follow at once so the next strike reaches the new module.
bool DrumKitPadsSection::editPadUndoably(const std::function<bool()>& change) {
    const bool record = host_.undo != nullptr && host_.graph != nullptr;
    if (record)
        host_.undo->captureBeforeState(*host_.graph);
    const bool changed = change();
    if (record) {
        if (changed)
            host_.undo->pushSnapshotFromCapture(*host_.graph);
        else
            host_.undo->takeCapturedGraphBeforeState();
    }
    if (changed)
        refreshFromKit();
    return changed;
}

bool DrumKitPadsSection::setPadFileUndoably(int index, const juce::File& file) {
    const auto was = kit_.pad(index).name; // the file renames the pad; the hint names the pad the user aimed at
    const bool done = editPadUndoably([&] { return kit_.setPadFile(index, file); });
    if (done)
        DrumPadSoundMenu::rememberFile(file.getFullPathName());
    hint_ = done ? was + " now plays " + file.getFileName() : "Could not read " + file.getFileName();
    hintUntil_ = juce::Time::getMillisecondCounter() + (juce::uint32)kHintMs;
    refreshCaption();
    return done;
}

void DrumKitPadsSection::setInstrumentPitchUndoably(int index, int pitch) {
    auto pad = kit_.pad(index);
    pitch = juce::jlimit(0, 127, pitch);
    if (pad.kind != PadKind::Instrument || pitch == pad.instrumentPitch)
        return;
    pad.instrumentPitch = pitch;
    editPadUndoably([&] { return kit_.setPad(index, pad); });
}

void DrumKitPadsSection::chooseFileFor(int index) {
    chooser_ = std::make_unique<juce::FileChooser>("Choose a sound for " + kit_.pad(index).name, juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    juce::Component::SafePointer<juce::Component> card(host_.card);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, card, index](const juce::FileChooser& chooser) {
                              if (card != nullptr && chooser.getResult() != juce::File())
                                  setPadFileUndoably(index, chooser.getResult());
                          });
}

void DrumKitPadsSection::relinkInstruments() {
    if (host_.graph != nullptr)
        synth::publishSamplerMidiWiring(*host_.graph);
}

// ---- Instrument pads ----

juce::AudioProcessorGraph::Node* DrumKitPadsSection::nodeForUuid(const juce::String& uuid) const {
    if (host_.graph == nullptr || uuid.isEmpty())
        return nullptr;
    for (auto* node : host_.graph->getNodes())
        if (node != nullptr && node->properties["uuid"].toString() == uuid)
            return node;
    return nullptr;
}

void DrumKitPadsSection::showInstrument(int index) {
    if (auto* node = nodeForUuid(kit_.pad(index).instrumentUuid); node != nullptr && host_.showNode)
        host_.showNode(node->nodeID);
}

std::pair<juce::String, juce::String> DrumKitPadsSection::describeSound(int index) const {
    const auto& pad = kit_.pad(index);
    switch (pad.kind) {
    case PadKind::File:
        return {juce::File(pad.file).getFileName(), "audio file"};
    case PadKind::Instrument:
        if (auto* node = nodeForUuid(pad.instrumentUuid); node != nullptr && kit_.padInstrumentLinked(index))
            return {synth::moduleTitle(*node), "instrument"};
        return {pad.name, "removed, silent"};
    case PadKind::Kit:
        if (kit_.padPlaysRecording(index))
            return {synth::drums::soundName(pad.sound).isNotEmpty() ? synth::drums::soundName(pad.sound) : pad.name,
                    "kit"};
        break;
    case PadKind::BuiltIn:
        break;
    }
    const auto name = synth::drums::soundName(pad.sound);
    return name.isNotEmpty() ? std::make_pair(name, juce::String("built-in"))
                             : std::make_pair(juce::String("No sound"), juce::String());
}

// What a drop would do, shown on the caption line while a sound is dragged.
juce::String DrumKitPadsSection::dropHint() const {
    if (!grid_.isShowingSoundOutline())
        return {};
    const int target = grid_.dropTarget();
    return target >= 0 ? "Drop to make " + kit_.pad(target).name + " play this sound"
                       : "Drop on a pad to play this sound there";
}

} // namespace synth::ui
