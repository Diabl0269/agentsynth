// DrumPadSoundMenu.cpp -- the pad's Sound groups and pad menu. Every item id indexes a Choice recorded while the menu
// is built, so a pick resolves against what the menu showed even if the canvas changed while it was open.
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadSoundMenu.h"
#include "AudioEngine/ModuleTitle.h"

namespace synth::ui {

using synth::drums::PadKind;
using Kind = DrumPadSoundMenu::Choice::Kind;

namespace {

// The built-in drums the top of the group offers; the rest go in the submenu.
constexpr std::array<int, 3> kMainDrums = {36, 38, 39};

juce::StringArray& recentFiles() {
    static juce::StringArray files;
    return files;
}

bool isMainDrum(int sound) { return std::find(kMainDrums.begin(), kMainDrums.end(), sound) != kMainDrums.end(); }

} // namespace

std::vector<DrumPadSoundMenu::Instrument> DrumPadSoundMenu::instrumentsIn(juce::AudioProcessorGraph* graph) {
    std::vector<Instrument> out;
    if (graph == nullptr)
        return out;
    for (auto* node : graph->getNodes()) {
        const auto* module = node != nullptr ? dynamic_cast<const ModuleBase*>(node->getProcessor()) : nullptr;
        if (module != nullptr && DrumKitModule::canPlayFromPad(module->getModuleType()))
            out.push_back({node->nodeID, node->properties["uuid"].toString(), synth::moduleTitle(*node)});
    }
    return out;
}

void DrumPadSoundMenu::rememberFile(const juce::String& path) {
    auto& files = recentFiles();
    files.removeString(path);
    files.insert(0, path);
    files.removeRange(kMaxRecent, files.size());
}

int DrumPadSoundMenu::add(const Choice& choice) {
    choices_.push_back(choice);
    return (int)choices_.size();
}

DrumPadSoundMenu::Choice DrumPadSoundMenu::choiceFor(int result) const {
    if (result < 1 || result > (int)choices_.size())
        return {};
    return choices_[(size_t)result - 1];
}

// Recent files are this session's pad files plus the files this kit's other pads play, newest first.
void DrumPadSoundMenu::addFileGroup(juce::PopupMenu& menu, const DrumKitModule& kit, int index) {
    menu.addSectionHeader(kFileGroup);
    menu.addItem(add({Kind::ChooseFile}), "Choose a file...");
    juce::StringArray recent = recentFiles();
    for (int i = 0; i < synth::drums::kMaxPads; ++i)
        if (const auto& p = kit.pad(i); p.kind == PadKind::File && p.file.isNotEmpty())
            recent.addIfNotAlreadyThere(p.file);
    juce::PopupMenu files;
    const auto& pad = kit.pad(index);
    for (int i = 0; i < juce::jmin(recent.size(), kMaxRecent); ++i) {
        Choice choice{Kind::File};
        choice.file = recent[i];
        const bool current = pad.kind == PadKind::File && pad.file == recent[i];
        files.addItem(add(choice), juce::File(recent[i]).getFileName(), true, current);
    }
    menu.addSubMenu("Recent files", files, files.getNumItems() > 0);
}

void DrumPadSoundMenu::addBuiltInGroup(juce::PopupMenu& menu, const synth::drums::Pad& pad) {
    menu.addSectionHeader(kBuiltInGroup);
    juce::PopupMenu more;
    for (const int sound : synth::drums::builtInSounds()) {
        Choice choice{Kind::BuiltIn};
        choice.sound = sound;
        const bool current = pad.kind == PadKind::BuiltIn && pad.sound == sound;
        auto& into = isMainDrum(sound) ? menu : more;
        into.addItem(add(choice), synth::drums::soundName(sound), true, current);
    }
    menu.addSubMenu(kMoreDrums, more);
}

void DrumPadSoundMenu::addInstrumentGroup(juce::PopupMenu& menu, const synth::drums::Pad& pad,
                                          const std::vector<Instrument>& instruments) {
    menu.addSectionHeader(kInstrumentGroup);
    if (instruments.empty())
        menu.addItem(-1, "No instruments on the canvas yet", false);
    for (const auto& instrument : instruments) {
        Choice choice{Kind::Instrument};
        choice.node = instrument.node;
        const bool current =
            pad.kind == PadKind::Instrument && instrument.uuid.isNotEmpty() && pad.instrumentUuid == instrument.uuid;
        menu.addItem(add(choice), instrument.title, true, current);
    }
}

// The kit's sounds: every built-in drum the loaded kit has a recording for.
void DrumPadSoundMenu::addKitGroup(juce::PopupMenu& menu, const DrumKitModule& kit, const synth::drums::Pad& pad) {
    menu.addSectionHeader(kKitGroup);
    if (!kit.hasRecordedKit()) {
        menu.addItem(-1, "No kit loaded", false);
        return;
    }
    juce::PopupMenu sounds;
    for (const int sound : synth::drums::builtInSounds()) {
        if (!kit.kitHasSound(sound))
            continue;
        Choice choice{Kind::KitSound};
        choice.sound = sound;
        const bool current = pad.kind == PadKind::Kit && pad.sound == sound;
        sounds.addItem(add(choice), synth::drums::soundName(sound), true, current);
    }
    menu.addSubMenu(kit.getInstrumentName().isNotEmpty() ? kit.getInstrumentName() : juce::String("Kit sounds"), sounds,
                    sounds.getNumItems() > 0);
}

void DrumPadSoundMenu::addGroups(juce::PopupMenu& menu, const DrumKitModule& kit, int index,
                                 const std::vector<Instrument>& instruments) {
    const auto& pad = kit.pad(index);
    addFileGroup(menu, kit, index);
    addBuiltInGroup(menu, pad);
    addInstrumentGroup(menu, pad, instruments);
    addKitGroup(menu, kit, pad);
}

juce::PopupMenu DrumPadSoundMenu::buildSoundMenu(const DrumKitModule& kit, int index,
                                                 const std::vector<Instrument>& instruments) {
    choices_.clear();
    juce::PopupMenu menu;
    addGroups(menu, kit, index, instruments);
    menu.addSeparator();
    menu.addItem(add({Kind::Reset}), "Reset to kit sound");
    return menu;
}

// The right-click and Shift+F10 menu: the pad's own actions first, then the same groups the Sound row opens.
juce::PopupMenu DrumPadSoundMenu::buildPadMenu(const DrumKitModule& kit, int index,
                                               const std::vector<Instrument>& instruments) {
    choices_.clear();
    juce::PopupMenu menu;
    menu.addItem(add({Kind::Play}), "Play");
    menu.addItem(add({Kind::ChooseFile}), "Replace from file...");
    menu.addItem(add({Kind::Reset}), "Reset to kit sound");
    addGroups(menu, kit, index, instruments);
    return menu;
}

} // namespace synth::ui
