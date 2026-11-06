// MainComponentCommandPalette.cpp -- the command palette's lists and what a pick runs. The rows are listed fresh each
// time it opens (a module added since shows up), ranked by CommandPaletteRows, and every pick runs the code the same
// thing runs elsewhere: an action its command, a view its toggle, a "+" row the Add list's own pick.
#include "MainComponentCommandPalette.h"

#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ModuleTitle.h"
#include "MainComponent.h"
#include "MidiRemote/MidiRemotePreferences.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "ShortcutManager/AppCommands.h"
#include "Transport/TransportNudge.h"
#include "UI/AddList/AddList.h"
#include "UI/CommandPalette/CommandPaletteComponent.h"
#include "UI/CommandPalette/CommandPaletteRows.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/AppTooltipWindow.h"
#include "UI/Library/LibrarySidebarSwitch.h"
#include "UI/Library/LibrarySidebarViews.h"
#include "UI/Timeline/DeleteTrackConfirm.h"
#include "UI/Timeline/TimelinePanelComponent/AddTrackPicker.h"
#include "UI/Timeline/TimelineTransportBar.h"
#include "UserSettings.h"

using synth::ui::PaletteEntry;
using synth::ui::PaletteKind;
using Tab = synth::ui::BottomDockComponent::Tab;

namespace {

constexpr int kTopGap = 8; // between the top bar and the palette's default spot
constexpr const char* kPaletteTitle = "Search everything";
constexpr const char* kSearchTitle = "Search actions, views, modules, tracks and settings";
constexpr const char* kSearchPlaceholder = "Search everything, or type + to add";

// The on/off preferences the palette flips in place: the same key, default and live effect as their Preferences
// checkbox. A setting read at use needs no apply.
struct SwitchSpec {
    const char* key;
    const char* label;
    const char* keywords;
    bool defaultOn;
    void (*apply)(MainComponent&, bool);
};

} // namespace

static const std::vector<SwitchSpec>& switchSpecs(); // the table sits at the end of this file

struct MainComponentCommandPalette::AddRows {
    std::vector<synth::ui::ModMatrixPicker::Item> items;
};

MainComponentCommandPalette::MainComponentCommandPalette(MainComponent& owner)
    : mc_(owner) {}

MainComponentCommandPalette::~MainComponentCommandPalette() = default;

MainComponentCommandPalette& MainComponentCommandPalette::of(MainComponent& mc) {
    if (mc.commandPalette_ == nullptr)
        mc.commandPalette_ = std::make_unique<MainComponentCommandPalette>(mc);
    return *mc.commandPalette_;
}

void MainComponentCommandPalette::toggle(MainComponent& mc) {
    auto& palette = of(mc);
    if (palette.isOpen())
        palette.close();
    else
        palette.open();
}

// ---- Opening and closing ----

// The bar is a child of the main window, over everything, where the user last dragged it (else centred under the top
// bar). Keyboard focus goes to its search field and comes back on close.
void MainComponentCommandPalette::open() {
    if (isOpen())
        return;
    palette_.reset(); // a close still fading out ends here
    closing_ = false;
    focusBeforeOpen_ = mc_.getFocusedComponentForMenu();
    addRows_.reset();
    rebuildEntries();

    auto picker = std::make_unique<synth::ui::ModMatrixPicker>(
        "command", std::vector<synth::ui::ModMatrixPicker::Item>{}, -1, [this](int id) { pick(id); });
    picker->setAccessibleNames(kPaletteTitle, kSearchTitle);
    picker->setSearchPlaceholder(kSearchPlaceholder);
    picker->onRowAction = [this](int id) { pressRowAction(id); };
    picker->onDismissRequested = [this] { close(); };

    palette_ = std::make_unique<synth::ui::CommandPaletteComponent>(std::move(picker));
    palette_->onMoved = [this](juce::Point<int> topLeft) { savePosition(topLeft); };
    // Rows go in once the bar has sized the picker: filled at its empty-list height, the first row's scroll-into-view
    // would leave the list scrolled past its first header.
    palette_->getPicker().setRowSource([this](const juce::String& query) {
        const auto recent = juce::StringArray::fromLines(
            mc_.appProperties.getUserSettings()->getValue(synth::kCommandPaletteRecentSettingKey));
        static const std::vector<synth::ui::ModMatrixPicker::Item> none;
        return synth::ui::buildCommandPaletteRows(entries_, synth::ui::isAddListQuery(query) ? addRows().items : none,
                                                  recent, query);
    });
    palette_->setTopLeftPosition(startPosition());
    mc_.addChildComponent(*palette_);
    palette_->toFront(false);
    palette_->fadeIn();
    palette_->getPicker().grabKeyboardFocus();
}

void MainComponentCommandPalette::close() {
    if (palette_ == nullptr || closing_)
        return;
    closing_ = true;
    restoreFocus();
    // The close runs inside the picker's own key or pick handler, so the bar is freed on a later message, once its
    // fade has ended.
    juce::Component::SafePointer<synth::ui::CommandPaletteComponent> faded(palette_.get());
    palette_->fadeOut([this, faded] {
        juce::MessageManager::callAsync([this, faded, safeOwner = juce::Component::SafePointer<MainComponent>(&mc_)] {
            if (safeOwner != nullptr && faded != nullptr && faded.getComponent() == palette_.get() && closing_) {
                palette_.reset();
                closing_ = false;
            }
        });
    });
}

void MainComponentCommandPalette::restoreFocus() {
    focusRestoredForTest_ = nullptr;
    auto* target = focusBeforeOpen_.getComponent();
    // Gone, or hidden since (a panel the pick closed): the window keeps whatever focus it has.
    if (target == nullptr || !target->isVisible())
        return;
    focusRestoredForTest_ = target;
    target->grabKeyboardFocus();
}

juce::Point<int> MainComponentCommandPalette::startPosition() const {
    const auto bounds = mc_.getLocalBounds();
    const auto saved = juce::StringArray::fromTokens(
        mc_.appProperties.getUserSettings()->getValue(synth::kCommandPalettePositionSettingKey), ",", "");
    if (saved.size() == 2)
        return synth::ui::CommandPaletteComponent::clampedPosition({saved[0].getIntValue(), saved[1].getIntValue()},
                                                                   bounds);
    return synth::ui::CommandPaletteComponent::defaultPosition(bounds, mc_.toolbar.getBottom() + kTopGap);
}

void MainComponentCommandPalette::savePosition(juce::Point<int> topLeft) {
    auto* settings = mc_.appProperties.getUserSettings();
    settings->setValue(synth::kCommandPalettePositionSettingKey,
                       juce::String(topLeft.x) + "," + juce::String(topLeft.y));
    settings->saveIfNeeded();
}

// ---- Picks ----

void MainComponentCommandPalette::pick(int id) {
    const auto index = synth::ui::paletteEntryIndexForId(id);
    if (!index) {
        // An Add list row: the Add list's own pick, as from a click (the palette already handed focus back).
        if (libraryRows_ != nullptr)
            runAddListPick(mc_, *libraryRows_, id, false, false, {});
        return;
    }
    const auto i = (size_t)*index;
    if (i >= entries_.size())
        return;
    if (entries_[i].switchOn) {
        flipSetting(i);
        return;
    }
    rememberPick(entries_[i].key);
    if (auto run = runners_[i])
        run();
}

// The window button on a plugin track's row: opens or closes that plugin's window.
void MainComponentCommandPalette::pressRowAction(int id) {
    const auto index = synth::ui::paletteEntryIndexForId(id);
    if (!index || (size_t)*index >= entryTracks_.size() || !entryTracks_[(size_t)*index].isValid())
        return;
    const auto track = entryTracks_[(size_t)*index];
    if (togglePluginWindowForTest)
        togglePluginWindowForTest(track);
    else
        mc_.toggleTrackPluginWindow(track);
}

void MainComponentCommandPalette::flipSetting(size_t entryIndex) {
    auto& entry = entries_[entryIndex];
    const bool on = !*entry.switchOn;
    entry.switchOn = on;
    for (const auto& spec : switchSpecs())
        if (entry.key == juce::String("setting:") + spec.key) {
            auto* settings = mc_.appProperties.getUserSettings();
            settings->setValue(spec.key, on ? "1" : "0");
            settings->saveIfNeeded();
            if (spec.apply != nullptr)
                spec.apply(mc_, on);
        }
    rememberPick(entry.key);
}

void MainComponentCommandPalette::rememberPick(const juce::String& key) {
    auto* settings = mc_.appProperties.getUserSettings();
    const auto recent = juce::StringArray::fromLines(settings->getValue(synth::kCommandPaletteRecentSettingKey));
    settings->setValue(synth::kCommandPaletteRecentSettingKey,
                       synth::ui::pushRecentPaletteKey(recent, key).joinIntoString("\n"));
    settings->saveIfNeeded();
}

// The Add list's pick, shared by the Add list and the palette's "+" rows. A library row is activated by identity; a
// track row is added on the Timeline, which slides into view first when it is not on screen.
void MainComponentCommandPalette::runAddListPick(MainComponent& mc,
                                                 const std::vector<synth::ui::AddListLibraryRow>& libraryRows, int id,
                                                 bool fromKeyboard, bool fromCanvas,
                                                 const std::function<void()>& handBackFocus) {
    if (const auto index = synth::ui::libraryIndexForAddListId(id)) {
        if (*index < (int)libraryRows.size())
            mc.moduleLibrary.activateAddListRow(libraryRows[(size_t)*index]);
        if (fromKeyboard && !fromCanvas)
            mc.focusRegions_.focusRegionById("canvas");
        else if (handBackFocus)
            handBackFocus();
        return;
    }
    juce::Component::SafePointer<MainComponent> safe(&mc);
    auto addTrack = [safe, id, fromKeyboard, fromCanvas] {
        if (safe == nullptr)
            return;
        safe->timelinePanel.finishAddTrackMenu(id, fromKeyboard && !fromCanvas);
        if (fromKeyboard && fromCanvas)
            safe->canvasToolbar_.focusSlot(synth::ui::CanvasToolbar::Add);
    };
    if (mc.isViewOnScreen(Tab::Timeline)) {
        addTrack();
        return;
    }
    mc.showBottomDockTab(Tab::Timeline);
    mc.timelineSlide_.whenSettled(addTrack);
}

// ---- The lists ----

void MainComponentCommandPalette::rebuildEntries() {
    entries_.clear();
    runners_.clear();
    entryTracks_.clear();
    addPlaceEntries();
    addModuleEntries();
    addTrackAndMarkerEntries();
    addActionEntries();
    addSettingEntries();
    addFileEntries();
    entryTracks_.resize(entries_.size());
}

// The "+" rows are the Add list's own, read the first time a query asks for them.
const MainComponentCommandPalette::AddRows& MainComponentCommandPalette::addRows() {
    if (addRows_ == nullptr) {
        libraryRows_ = std::make_shared<std::vector<synth::ui::AddListLibraryRow>>(mc_.moduleLibrary.getAddListRows());
        const bool canvasFirst = mc_.isViewOnScreen(Tab::Canvas) || !mc_.isViewOnScreen(Tab::Timeline);
        addRows_ = std::make_unique<AddRows>();
        addRows_->items = synth::ui::buildAddListItems(
            synth::ui::flattenAddTrackMenu(mc_.timelinePanel.buildAddMenuWithChannels()), *libraryRows_,
            canvasFirst ? synth::ui::AddListOrigin::Canvas : synth::ui::AddListOrigin::Timeline);
    }
    return *addRows_;
}

// Every rebindable command, under its Settings name and with its key; one that cannot run now is listed greyed out.
void MainComponentCommandPalette::addActionEntries() {
    for (const auto& actionId : mc_.shortcutManager.getActionIds()) {
        const auto command = AppCommands::getCommandForAction(actionId);
        if (command == AppCommands::kNoCommand || command == AppCommands::openCommandPalette)
            continue;
        PaletteEntry entry;
        entry.kind = PaletteKind::Action;
        entry.key = "action:" + actionId;
        entry.text = ShortcutManager::getActionDescription(actionId);
        entry.hint = ShortcutManager::keyPressToDisplayString(mc_.shortcutManager.getBinding(actionId));
        for (const auto& spec : mc_.commandTable())
            if (spec.id == command && spec.isActive)
                entry.enabled = spec.isActive(mc_);
        entries_.push_back(entry);
        runners_.push_back([this, command] { mc_.commandManager.invokeDirectly(command, true); });
    }
}

void MainComponentCommandPalette::addPlaceEntries() {
    const auto place = [this](const char* key, const char* text, const char* keywords, std::function<void()> run) {
        PaletteEntry entry;
        entry.kind = PaletteKind::Place;
        entry.key = juce::String("place:") + key;
        entry.text = text;
        entry.keywords = keywords;
        entries_.push_back(entry);
        runners_.push_back(std::move(run));
    };
    auto& m = mc_;
    place("canvas", "Canvas", "patch modules cards graph show", [&m] {
        m.ensureCanvasShowing();
        m.focusRegions_.focusRegionById("canvas");
    });
    place("timeline", "Timeline", "arrange tracks clips song show",
          [&m] { m.showBottomDockTabFromShortcut(Tab::Timeline); });
    place("mixer", "Mixer", "channels faders levels show", [&m] { m.showBottomDockTabFromShortcut(Tab::Mixer); });
    place("midiRemote", "MIDI Remote", "controller mapping learn show",
          [&m] { m.showBottomDockTabFromShortcut(Tab::MidiRemote); });
    place("aiPanel", "AI panel", "assistant chat show", [&m] { m.focusRegions_.focusRegionById("aiPanel"); });
    place("modMatrix", "Mod Matrix", "modulation matrix show", [&m] {
        m.ensureCanvasShowing();
        if (!m.graphEditor.isModMatrixVisible())
            m.commandManager.invokeDirectly(AppCommands::toggleModMatrix, false);
        m.focusRegions_.focusRegionById("modMatrix");
    });
    place("library", "Library", "modules browser sidebar show", [&m] {
        if (!m.isLibraryVisible || m.librarySidebar_->getView() != synth::ui::SidebarView::Modules)
            m.toggleLibraryView(synth::ui::SidebarView::Modules);
        m.focusRegions_.focusRegionById("library");
    });
    place("sounds", "Sounds", "samples browser sidebar library show", [&m] {
        if (!m.isLibraryVisible || m.librarySidebar_->getView() != synth::ui::SidebarView::Sounds)
            m.toggleLibraryView(synth::ui::SidebarView::Sounds);
        m.focusRegions_.focusRegionById("library");
    });
    for (const auto* tab :
         {"Audio", "AI", "Account", "Keyboard Shortcuts", "Preferences", "Appearance", "Feedback", "Sound packs"}) {
        const juce::String name(tab);
        PaletteEntry entry;
        entry.kind = PaletteKind::Place;
        entry.key = "settingsTab:" + name;
        entry.text = name + " settings";
        entry.keywords = "settings preferences tab open";
        entries_.push_back(entry);
        runners_.push_back([&m, name] { m.launchSettingsWindow(name); });
    }
}

// Every module on the canvas by the title its card shows; a pick selects the card and centres the view on it.
void MainComponentCommandPalette::addModuleEntries() {
    for (auto* node : mc_.audioEngine.getGraph().getNodes()) {
        if (node == nullptr || dynamic_cast<const ModuleBase*>(node->getProcessor()) == nullptr)
            continue;
        const auto uuid = node->properties["uuid"].toString();
        const auto title = synth::moduleTitle(*node);
        if (uuid.isEmpty() || title.isEmpty())
            continue;
        PaletteEntry entry;
        entry.kind = PaletteKind::Module;
        entry.key = "module:" + uuid;
        entry.text = title;
        entry.keywords = "focus show select card";
        entries_.push_back(entry);
        runners_.push_back([this, uuid] { mc_.showNodeOnCanvas(uuid); });
    }
}

// The window button's chord and name on a plugin track's row.
static synth::ui::ModMatrixPicker::RowAction pluginWindowAction(const juce::String& trackName,
                                                                const juce::String& chord) {
    synth::ui::ModMatrixPicker::RowAction action;
    action.title = "Show plugin window, " + trackName;
    action.tooltip =
        "Show or hide this track's plugin window" + (chord.isEmpty() ? juce::String() : " (" + chord + ")");
    action.icon = synth::theme::Icon::ActionDetachWindow;
    return action;
}

void MainComponentCommandPalette::addTrackAndMarkerEntries() {
    const auto chord = ShortcutManager::keyPressToDisplayString(
        mc_.shortcutManager.getBinding("timelineToggleFocusedTrackPluginWindow"));
    auto showTimelineThen = [this](std::function<void()> then) {
        if (mc_.isViewOnScreen(Tab::Timeline)) {
            then();
            return;
        }
        mc_.showBottomDockTab(Tab::Timeline);
        mc_.timelineSlide_.whenSettled(std::move(then));
    };
    for (const auto& track : mc_.timelineDoc.getTracks()) {
        PaletteEntry entry;
        entry.kind = PaletteKind::Track;
        entry.key = "track:" + juce::String(track.id.value);
        entry.text = track.name.isEmpty() ? juce::String("Track") : track.name;
        entry.keywords = "focus show row";
        const auto id = track.id;
        const bool hasPlugin = isPluginTrackForTest ? isPluginTrackForTest(id) : [&] {
            auto* node = mc_.trackModuleNode(id);
            return node != nullptr && dynamic_cast<synth::HostedPluginModule*>(node->getProcessor()) != nullptr;
        }();
        if (hasPlugin)
            entry.action = pluginWindowAction(entry.text, chord);
        entries_.push_back(entry);
        entryTracks_.resize(entries_.size());
        entryTracks_.back() = hasPlugin ? id : synth::TrackId{};
        runners_.push_back([this, id, showTimelineThen] {
            juce::Component::SafePointer<MainComponent> safe(&mc_);
            showTimelineThen([safe, id] {
                if (safe != nullptr)
                    safe->timelinePanel.focusTrackRow(id);
            });
        });
    }
    const double beatsPerBar = synth::beatsPerBarOf(mc_.audioEngine.getTransport().getPositionSnapshot());
    for (const auto& marker : mc_.timelineDoc.getMarkers()) {
        PaletteEntry entry;
        entry.kind = PaletteKind::Marker;
        entry.key = "marker:" + juce::String(marker.id.value);
        entry.text = marker.text.isEmpty() ? juce::String("Marker") : marker.text;
        entry.detail = "Bar " + juce::String((int)std::floor(marker.beat / beatsPerBar) + 1);
        entry.keywords = "jump playhead locate go to";
        entries_.push_back(entry);
        const double beat = marker.beat;
        runners_.push_back([this, beat] { mc_.audioEngine.getTransport().locateBeat(beat); });
    }
}

void MainComponentCommandPalette::addSettingEntries() {
    auto* settings = mc_.appProperties.getUserSettings();
    for (const auto& spec : switchSpecs()) {
        PaletteEntry entry;
        entry.kind = PaletteKind::Setting;
        entry.key = juce::String("setting:") + spec.key;
        entry.text = spec.label;
        entry.keywords = juce::String(spec.keywords) + " on off toggle preference";
        entry.switchOn = settings->getBoolValue(spec.key, spec.defaultOn);
        entries_.push_back(entry);
        runners_.push_back({});
    }
    // Settings that are not on/off open the Settings tab that holds them.
    struct TabSetting {
        const char* text;
        const char* keywords;
        const char* tab;
    };
    for (const auto& s : {TabSetting{"Audio device", "output input sample rate buffer interface", "Audio"},
                          TabSetting{"Theme", "colours dark light look appearance", "Appearance"},
                          TabSetting{"Keyboard shortcuts", "keys bindings chords rebind", "Keyboard Shortcuts"},
                          TabSetting{"Smart connections", "auto wire cables connect", "Preferences"},
                          TabSetting{"Autosave interval", "minutes backups", "Preferences"},
                          TabSetting{"AI model", "assistant provider key", "AI"}}) {
        PaletteEntry entry;
        entry.kind = PaletteKind::Setting;
        entry.key = juce::String("settingTab:") + s.text;
        entry.text = s.text;
        entry.keywords = juce::String(s.keywords) + " settings";
        entry.detail = juce::String(s.tab) + " settings";
        entries_.push_back(entry);
        const juce::String tab(s.tab);
        runners_.push_back([this, tab] { mc_.launchSettingsWindow(tab); });
    }
}

void MainComponentCommandPalette::addFileEntries() {
    for (const auto& file : mc_.getRecentProjects().getEntries()) {
        PaletteEntry entry;
        entry.kind = PaletteKind::Project;
        entry.key = "project:" + file.getFullPathName();
        entry.text = file.getFileNameWithoutExtension();
        entry.detail = file.getParentDirectory().getFileName();
        entry.keywords = "open recent project";
        entries_.push_back(entry);
        runners_.push_back([this, file] { mc_.openRecentProjectGuarded(file); });
    }
    const auto presets = synth::PresetManager::getPresetList();
    for (int i = 0; i < presets.size(); ++i) {
        PaletteEntry entry;
        entry.kind = PaletteKind::Preset;
        entry.key = "preset:" + presets[i].name;
        entry.text = presets[i].name;
        entry.detail = presets[i].category;
        entry.keywords = "load patch preset";
        entries_.push_back(entry);
        runners_.push_back([this, i] { mc_.loadPresetGuarded(i); });
    }
}

// Each apply mirrors its Preferences checkbox's persist function, minus the settings write the caller already made.
static const std::vector<SwitchSpec>& switchSpecs() {
    static const std::vector<SwitchSpec> specs{
        {"showCurvesOnCables", "Show curves on cables", "modulation cable curve button", false,
         [](MainComponent& m, bool on) {
             m.getGraphEditor().showCurvesOnCables = on;
             m.getGraphEditor().notifyModuleContentChanged();
         }},
        {"cablesOnTop", "Draw cables on top of modules", "cables front behind cards", false,
         [](MainComponent& m, bool on) {
             m.getGraphEditor().cablesOnTop = on;
             m.getGraphEditor().repaint();
         }},
        {"alignmentGuidesEnabled", "Show alignment guides", "snap guides drag canvas", true,
         [](MainComponent& m, bool on) { m.getGraphEditor().setAlignmentGuidesEnabled(on); }},
        {"doubleClickPortDisconnect", "Double-click port to disconnect", "jack cable unplug", true,
         [](MainComponent& m, bool on) { m.getGraphEditor().setDoubleClickPortDisconnectEnabled(on); }},
        {"reconnectChainOnDelete", "Reconnect the chain when deleting a module", "delete cables bridge", true,
         [](MainComponent& m, bool on) { m.getGraphEditor().setReconnectChainOnDeleteEnabled(on); }},
        {MainComponent::kNaturalScrollingKey, "Natural scrolling", "scroll direction trackpad", true,
         [](MainComponent& m, bool) { m.applyNaturalScrollingPreference(); }},
        {MainComponent::kZoomScrollUpZoomsInKey, "Scroll up to zoom in", "zoom wheel direction", true,
         [](MainComponent& m, bool) { m.applyZoomScrollPreference(); }},
        {"autosaveEnabled", "Autosave", "save backup crash recovery", true, nullptr},
        {synth::ui::kShowInfoTooltipsKey, "Show info tooltips", "help hover tips", true, nullptr},
        {synth::ui::kRecordStartsPlaybackKey, "Record also starts playback", "transport recording", true, nullptr},
        {synth::ui::kAskBeforeDeletingTrackKey, "Ask before deleting a track or its macro", "confirm delete", true,
         nullptr},
        {synth::kMidiRemoteShowBadgesSettingKey, "Show MIDI badges on mapped controls", "midi remote learn", true,
         [](MainComponent& m, bool) { m.applyMidiRemotePreferences(); }},
    };
    return specs;
}
