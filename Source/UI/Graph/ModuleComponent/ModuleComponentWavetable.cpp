// ModuleComponentWavetable.cpp -- the Wavetable oscillator's control creation, its tabbed page
// strip (pinned controls, per-page grouping table, tab layout), and the load/browse/step-through
// flow for wavetable files and folders. ModuleComponent is declared in ModuleComponent.h; the
// rest of its implementation lives in the sibling ModuleComponent*.cpp units next to this one
// (FRO65 split of the former single ModuleComponent.cpp).
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

    // A card dropped after the user has already browsed somewhere starts pointed at that
    // folder, so < and > work immediately instead of needing a folder pick per module.
    if (wtMod->getWavetableFolder() == juce::File()) {
        const juce::File remembered = owner.getLastWavetableFolder();
        if (remembered.isDirectory())
            wtMod->setWavetableFolder(remembered);
    }

    refreshWavetableLabel();
}

namespace {
// kTabPinned/kTabChrome (the sentinel page ids for controls that live outside the strip) are in
// ModuleComponentInternal.h: layoutDefaultContent (ModuleComponentLayout.cpp) also tests
// kTabChrome to decide whether a tabbed card has replaced the flat combo grid.

// Page titles, and the control names each page owns. Names are the parameter display names
// (`param->getName(100)`), which is what the slider/combo labels carry.
struct WavetablePage {
    const char* title;
    const char* members; // space-free, '|'-separated display names
};

const WavetablePage kWavetablePages[] = {
    {"Tune", "Octave|Coarse|Fine|Level"},
    {"Unison", "Unison|Detune|Stack|Blend|Width"},
    {"Phase", "Phase|Rand Phase|Spread"},
    {"Sub", "Sub|Sub Oct|Sub Wave|Pan|Sync In"},
    {"File", "Import|Interp"},
};
constexpr int kNumWavetablePages = (int)(sizeof(kWavetablePages) / sizeof(kWavetablePages[0]));

/** Page owning `name`, or kTabPinned / kTabChrome for the controls that live outside the strip. */
int wavetablePageFor(const juce::String& name) {
    // Position and Warp are what you actually perform with, so they stay above the strip;
    // Table belongs with the display it selects.
    if (name == "Position" || name == "Warp" || name == "Warp Amt")
        return kTabPinned;
    if (name == "Table")
        return kTabChrome;

    for (int page = 0; page < kNumWavetablePages; ++page)
        for (const auto& member : juce::StringArray::fromTokens(kWavetablePages[page].members, "|", ""))
            if (member == name)
                return page;

    return 0; // anything unclassified lands on the first page rather than vanishing
}
} // namespace

void ModuleComponent::createWavetableTabs() {
    if (dynamic_cast<WavetableOscillatorModule*>(module) == nullptr)
        return;

    sliderTabIndex.clearQuick();
    for (auto* label : sliderLabels)
        sliderTabIndex.add(wavetablePageFor(label->getText()));

    comboTabIndex.clearQuick();
    for (auto* label : comboLabels)
        comboTabIndex.add(wavetablePageFor(label->getText()));

    for (int page = 0; page < kNumWavetablePages; ++page) {
        auto* tab = wavetableTabs.add(new juce::TextButton(kWavetablePages[page].title));
        tab->setComponentID("wtTab" + juce::String(page));
        tab->setClickingTogglesState(true);
        tab->setRadioGroupId(1000 + (int)nodeId.uid);
        tab->setToggleState(page == activeWavetableTab, juce::dontSendNotification);
        tab->setConnectedEdges((page > 0 ? juce::Button::ConnectedOnLeft : 0) |
                               (page < kNumWavetablePages - 1 ? juce::Button::ConnectedOnRight : 0));
        tab->onClick = [this, page] {
            if (activeWavetableTab == page)
                return;
            activeWavetableTab = page;
            applyWavetableTabVisibility();
            resized();
            repaint();
        };
        addAndMakeVisible(tab);
    }

    applyWavetableTabVisibility();

    // createControls() ends by sizing the card, and it ran before this — so without a second
    // pass the card keeps the flat-grid height and the tabbed layout is never applied.
    updateLayout();
}

void ModuleComponent::applyWavetableTabVisibility() {
    if (wavetableTabs.isEmpty())
        return;

    const auto onActivePage = [this](int tab) { return tab == kTabPinned || tab == activeWavetableTab; };

    for (int i = 0; i < sliders.size(); ++i) {
        const bool show = i < sliderTabIndex.size() && onActivePage(sliderTabIndex[i]);
        sliders[i]->setVisible(show);
        sliderLabels[i]->setVisible(show);
    }
    for (int i = 0; i < comboBoxes.size(); ++i) {
        const bool show =
            i < comboTabIndex.size() && (comboTabIndex[i] == kTabChrome || onActivePage(comboTabIndex[i]));
        comboBoxes[i]->setVisible(show);
        comboLabels[i]->setVisible(show);
    }

    for (int page = 0; page < wavetableTabs.size(); ++page)
        wavetableTabs[page]->setToggleState(page == activeWavetableTab, juce::dontSendNotification);
}

int ModuleComponent::layoutWavetableTabs(int y, int contentX, int contentW, bool apply) {
    const int knobColumns = kKnobColumns * 2; // double-width card
    const int knobWidth = contentW / knobColumns;
    // Three across rather than two: no page has more than three combos, so this keeps every
    // page's selectors on one row and takes the tallest page (Sub) from 172px to 124px.
    const int comboColumns = 3;
    const int comboCellW = contentW / comboColumns;

    // --- Pinned row: the two performance controls, with Warp's mode selector between them ---
    {
        const int cellW = contentW / 3;
        int col = 0;
        for (int i = 0; i < sliders.size(); ++i) {
            if (sliderTabIndex[i] != kTabPinned)
                continue;
            if (apply) {
                const int x = contentX + (col == 0 ? 0 : cellW * 2);
                sliderLabels[i]->setBounds(x, y, cellW, kLabelHeight);
                sliders[i]->setBounds(x, y + kLabelHeight, cellW, kKnobHeight);
            }
            ++col;
        }
        for (int i = 0; i < comboBoxes.size(); ++i) {
            if (comboTabIndex[i] != kTabPinned)
                continue;
            if (apply) {
                // Label on the knobs' label line, combo vertically centred against the knobs, so
                // the three pinned controls read as one row rather than a stagger.
                const int x = contentX + cellW;
                comboLabels[i]->setBounds(x + 6, y, cellW - 12, kLabelHeight);
                comboBoxes[i]->setBounds(x + 6, y + kLabelHeight + (kKnobHeight - kRowHeight) / 2, cellW - 12,
                                         kRowHeight);
            }
        }
        y += kLabelHeight + kKnobHeight + 10;
    }

    // --- Tab strip ---
    if (apply) {
        const int tabW = contentW / std::max(1, wavetableTabs.size());
        for (int page = 0; page < wavetableTabs.size(); ++page)
            wavetableTabs[page]->setBounds(contentX + page * tabW, y, tabW, kRowHeight);
    }
    y += kRowHeight + 8;

    // --- Active page, measured against every page so the card never resizes on a tab switch ---
    int tallestPage = 0;
    for (int page = 0; page < kNumWavetablePages; ++page) {
        int pageCombos = 0, pageKnobs = 0;
        for (int i = 0; i < comboTabIndex.size(); ++i)
            if (comboTabIndex[i] == page)
                ++pageCombos;
        for (int i = 0; i < sliderTabIndex.size(); ++i)
            if (sliderTabIndex[i] == page)
                ++pageKnobs;

        const int comboRows = (pageCombos + comboColumns - 1) / comboColumns;
        const int knobRows = (pageKnobs + knobColumns - 1) / knobColumns;
        tallestPage = std::max(tallestPage,
                               comboRows * (kLabelHeight + kRowHeight + 6) + knobRows * (kLabelHeight + kKnobHeight));
    }

    if (apply) {
        int pageY = y;
        int comboSlot = 0;
        for (int i = 0; i < comboBoxes.size(); ++i) {
            if (comboTabIndex[i] != activeWavetableTab)
                continue;
            const int row = comboSlot / comboColumns;
            const int x = contentX + (comboSlot % comboColumns) * comboCellW;
            const int rowY = pageY + row * (kLabelHeight + kRowHeight + 6);
            comboLabels[i]->setBounds(x, rowY, comboCellW - 8, kLabelHeight);
            comboBoxes[i]->setBounds(x, rowY + kLabelHeight, comboCellW - 8, kRowHeight);
            ++comboSlot;
        }
        pageY += ((comboSlot + comboColumns - 1) / comboColumns) * (kLabelHeight + kRowHeight + 6);

        int pageKnobCount = 0;
        for (int i = 0; i < sliderTabIndex.size(); ++i)
            if (sliderTabIndex[i] == activeWavetableTab)
                ++pageKnobCount;

        int knobSlot = 0;
        for (int i = 0; i < sliders.size(); ++i) {
            if (sliderTabIndex[i] != activeWavetableTab)
                continue;
            const int row = knobSlot / knobColumns;
            const int col = knobSlot % knobColumns;

            // Centre each row. Most pages carry fewer than knobColumns knobs, and left-aligning
            // them stranded half the card's width as dead space.
            const int inThisRow = std::min(knobColumns, pageKnobCount - row * knobColumns);
            const int rowIndent = (contentW - inThisRow * knobWidth) / 2;

            const int x = contentX + rowIndent + col * knobWidth;
            const int rowY = pageY + row * (kLabelHeight + kKnobHeight);
            sliderLabels[i]->setBounds(x, rowY, knobWidth, kLabelHeight);
            sliders[i]->setBounds(x, rowY + kLabelHeight, knobWidth, kKnobHeight);
            ++knobSlot;
        }
    }

    return y + tallestPage + 6;
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
