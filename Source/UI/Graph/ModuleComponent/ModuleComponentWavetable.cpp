// ModuleComponentWavetable.cpp -- the Wavetable oscillator's card chrome (the frame display, the Table
// selector, the load and folder row) and the load/browse/step-through flow for wavetable files and
// folders. Its controls are the card body's, paged by the code default's tab sections
// (DefaultCardLayoutsSources.cpp). ModuleComponent is declared in ModuleComponent.h; the rest of its
// implementation lives in the sibling ModuleComponent*.cpp units.
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

using namespace detail;

void ModuleComponent::createWavetableControls() {
    auto* wtMod = dynamic_cast<WavetableOscillatorModule*>(module);
    if (wtMod == nullptr)
        return;

    wavetableDisplay = std::make_unique<WavetableDisplayComponent>(*wtMod);
    addAndMakeVisible(*wavetableDisplay);

    loadWavetableButton = std::make_unique<juce::TextButton>("Load Wavetable...");
    loadWavetableButton->setTooltip("Load an audio file as a wavetable, or drop one straight onto this card. The "
                                    "Import combo below decides how it is cut into frames.");
    loadWavetableButton->onClick = [this] { openWavetableChooser(); };
    addAndMakeVisible(*loadWavetableButton);

    wavetableFolderButton = std::make_unique<juce::TextButton>("Folder...");
    wavetableFolderButton->setTooltip("Pick a wavetable folder, then step through it with < and >");
    wavetableFolderButton->onClick = [this] { openWavetableFolderChooser(); };
    addAndMakeVisible(*wavetableFolderButton);

    wavetablePrevButton = std::make_unique<juce::TextButton>("<");
    wavetablePrevButton->setTooltip("Previous wavetable in the folder");
    wavetablePrevButton->onClick = [this] { stepWavetableBrowser(-1); };
    addAndMakeVisible(*wavetablePrevButton);

    wavetableNextButton = std::make_unique<juce::TextButton>(">");
    wavetableNextButton->setTooltip("Next wavetable in the folder");
    wavetableNextButton->onClick = [this] { stepWavetableBrowser(1); };
    addAndMakeVisible(*wavetableNextButton);

    wavetableNameLabel = std::make_unique<juce::Label>();
    wavetableNameLabel->setJustificationType(juce::Justification::centredLeft);
    wavetableNameLabel->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*wavetableNameLabel);

    if (auto* table = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(wtMod, "table")))
        createWavetableTableSelector(*table);

    // A card dropped after the user has already browsed somewhere starts pointed at that
    // folder, so < and > work immediately instead of needing a folder pick per module.
    if (wtMod->getWavetableFolder() == juce::File()) {
        const juce::File remembered = owner.getLastWavetableFolder();
        if (remembered.isDirectory())
            wtMod->setWavetableFolder(remembered);
    }

    refreshWavetableLabel();
}

// Everything the card body gives a choice: the comboBoxes/comboLabels/comboParams entries (value
// reflection, Automate, accessibility names), MIDI Learn with the card as the right-click listener,
// and the attachment. Built before the body's widgets, so it keeps the first combo slot it always had.
void ModuleComponent::createWavetableTableSelector(juce::AudioParameterChoice& table) {
    auto* combo = adoptBespokeWidget(new juce::ComboBox());
    combo->addItemList(table.choices, 1);
    addAndMakeVisible(combo);
    combo->addMouseListener(this, false);
    registerMidiLearnable(*combo, &table);
    wavetableTableAttachment = std::make_unique<juce::ComboBoxParameterAttachment>(table, *combo);
    comboBoxes.add(combo);
    comboParams.add(&table);

    auto* label = adoptBespokeWidget(new juce::Label(table.getName(100), table.getName(100)));
    addAndMakeVisible(label);
    comboLabels.add(label);
}

bool ModuleComponent::loadWavetableIntoModule(const juce::File& file) {
    auto* wtMod = dynamic_cast<WavetableOscillatorModule*>(module);
    if (wtMod == nullptr || !wtMod->loadWavetableFile(file))
        return false;

    // Switch the Table choice to "Loaded File" so the new table is what sounds.
    for (auto* param : wtMod->getParameters()) {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param)) {
            if (choice->paramID == "table" && choice->choices.size() > 1) {
                const float normalised =
                    (float)WavetableOscillatorModule::kLoadedTableChoice / (float)(choice->choices.size() - 1);
                choice->setValueNotifyingHost(normalised);
            }
        }
    }
    return true;
}

void ModuleComponent::stepWavetableBrowser(int delta) {
    auto* wtMod = dynamic_cast<WavetableOscillatorModule*>(module);
    if (wtMod == nullptr)
        return;

    if (wtMod->getFolderWavetableCount() == 0) {
        refreshWavetableLabel("No folder selected");
        return;
    }

    if (!wtMod->stepWavetable(delta)) {
        refreshWavetableLabel("No readable wavetables in folder");
        return;
    }

    // stepWavetable already loaded the file; only the Table choice still needs pointing at it.
    loadWavetableIntoModule(wtMod->getFolderWavetable(wtMod->getFolderIndex()));
    refreshWavetableLabel();
    repaint();
}

void ModuleComponent::refreshWavetableLabel(const juce::String& fallbackMessage) {
    if (wavetableNameLabel == nullptr)
        return;

    auto* wtMod = dynamic_cast<WavetableOscillatorModule*>(module);
    if (wtMod == nullptr)
        return;

    if (fallbackMessage.isNotEmpty()) {
        wavetableNameLabel->setText(fallbackMessage, juce::dontSendNotification);
        wavetableNameLabel->setTooltip(fallbackMessage);
        return;
    }

    const int count = wtMod->getFolderWavetableCount();
    const int index = wtMod->getFolderIndex();
    const juce::File file = wtMod->getWavetableFile();

    juce::String text = (file == juce::File()) ? juce::String("(built-in table)") : file.getFileName();
    if (count > 0 && index >= 0)
        text += "  " + juce::String(index + 1) + "/" + juce::String(count);
    else if (count > 0)
        text += "  -/" + juce::String(count);

    wavetableNameLabel->setText(text, juce::dontSendNotification);
    wavetableNameLabel->setTooltip(wtMod->getWavetableFolder().getFullPathName());
}

void ModuleComponent::openWavetableChooser() {
    auto* wtMod = dynamic_cast<WavetableOscillatorModule*>(module);
    const juce::File startIn = (wtMod != nullptr) ? wtMod->getWavetableFolder() : juce::File();

    wavetableChooser =
        std::make_unique<juce::FileChooser>("Load Wavetable", startIn, "*.wav;*.aiff;*.aif;*.flac;*.ogg");

    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    // SafePointer: the dialog is async, so this component (and its module) may be gone by
    // the time the user picks a file.
    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    wavetableChooser->launchAsync(flags, [safeThis](const juce::FileChooser& chooser) {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;

        const juce::File file = chooser.getResult();
        if (file == juce::File())
            return;

        if (!self->loadWavetableIntoModule(file)) {
            juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Load Wavetable",
                                                        "Could not read \"" + file.getFileName() +
                                                            "\" as a wavetable.");
            return;
        }

        self->refreshWavetableLabel();
        self->repaint();
    });
}

void ModuleComponent::openWavetableFolderChooser() {
    auto* wtMod = dynamic_cast<WavetableOscillatorModule*>(module);
    const juce::File startIn = (wtMod != nullptr) ? wtMod->getWavetableFolder() : juce::File();

    wavetableFolderChooser = std::make_unique<juce::FileChooser>("Choose a wavetable folder", startIn);

    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;

    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    wavetableFolderChooser->launchAsync(flags, [safeThis](const juce::FileChooser& chooser) {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;

        const juce::File folder = chooser.getResult();
        if (folder == juce::File() || !folder.isDirectory())
            return;

        auto* mod = dynamic_cast<WavetableOscillatorModule*>(self->getModule());
        if (mod == nullptr)
            return;

        mod->setWavetableFolder(folder);
        self->owner.rememberWavetableFolder(folder);
        self->refreshWavetableLabel();
        self->repaint();
    });
}
