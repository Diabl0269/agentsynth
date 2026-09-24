// ControllersListComponent.cpp -- FRO131 (docs/control/midi-remote-ui.md#controllers-list-left):
// paint, hit-testing and the right-click Rename/Export/Delete flow. See the header for the
// caller-facing contract.
//
// Test seams below (test_hooks namespace) are free functions with EXTERNAL linkage, not hook
// members on the class -- ControllersListComponent.h's contract is locked for this ticket (other
// agents are concurrently wiring the panel around it), so a member can't be added here. Each hook
// mirrors an existing hook-member idiom elsewhere in this codebase, just relocated to a free
// function so Tests/UI/MidiRemote/ControllersListTests.cpp can reach in via its own forward
// declaration of the same signature instead of widening the class:
//   - contextMenuHookForTest()   mirrors ModuleComponent::setShowContextMenuHookForTest (a real
//     juce::PopupMenu::showMenuAsync() segfaults on a headless Linux CI runner with no display).
//   - renamePromptHookForTest()  mirrors GraphEditor::promptRenameMacroForTest.
//   - deleteConfirmHookForTest() mirrors ExportAudioDialog::collisionPromptForTest.
// All three default to null (real behaviour); a test installs a stub, drives the real
// mouseDown()/right-click path, then clears the stub so it never leaks into a later test sharing
// the process.
#include "ControllersListComponent.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <algorithm>

namespace synth::ui {

namespace test_hooks {

std::function<void(juce::PopupMenu&)>& contextMenuHookForTest() {
    static std::function<void(juce::PopupMenu&)> hook;
    return hook;
}

std::function<void(const juce::String& currentName, std::function<void(const juce::String& resultText)> onChoice)>&
renamePromptHookForTest() {
    static std::function<void(const juce::String&, std::function<void(const juce::String&)>)> hook;
    return hook;
}

std::function<void(const juce::String& message, std::function<void(int result)> onChoice)>& deleteConfirmHookForTest() {
    static std::function<void(const juce::String&, std::function<void(int)>)> hook;
    return hook;
}

} // namespace test_hooks

namespace {

constexpr int kStateGlyphDiameter = 8;
constexpr int kActivityDotDiameter = 6;
constexpr int kRowPaddingH = 8;
constexpr int kGapPx = 6;

juce::String rowDisplayName(const ControllersListComponent::RowModel& row) {
    // docs/control/midi-remote-ui.md#controllers-list-left's exact wording for an orphan row.
    switch (row.state) {
    case ControllersListComponent::RowState::orphan:
        return row.name + " (not on this machine)";
    case ControllersListComponent::RowState::standaloneOnly:
        return row.name + " (standalone only)";
    default:
        return row.name;
    }
}

// present = normal text colour, absent = greyed (assignments kept, device just not connected --
// the doc's own wording), orphan = warning colour. Glyph and name text share this mapping.
juce::Colour rowStateColour(ControllersListComponent::RowState state, juce::Colour normalColour,
                            juce::Colour mutedColour, juce::Colour warningColour) {
    switch (state) {
    case ControllersListComponent::RowState::absent:
    case ControllersListComponent::RowState::standaloneOnly:
        return mutedColour;
    case ControllersListComponent::RowState::orphan:
        return warningColour;
    case ControllersListComponent::RowState::present:
    default:
        return normalColour;
    }
}

// One row's worth of drawing, factored out of paint() to keep it under the ~80-line function cap.
void paintRow(juce::Graphics& g, juce::Rectangle<int> bounds, const ControllersListComponent::RowModel& row,
              bool activityLit, bool selected, juce::Colour textColour, juce::Colour mutedColour,
              juce::Colour warningColour, juce::Colour accentColour) {
    if (selected) {
        g.setColour(accentColour.withAlpha(0.15f));
        g.fillRect(bounds);
    }

    auto content = bounds.reduced(kRowPaddingH, 4);

    auto dotArea =
        content.removeFromRight(kActivityDotDiameter).withSizeKeepingCentre(kActivityDotDiameter, kActivityDotDiameter);
    if (activityLit) {
        g.setColour(accentColour);
        g.fillEllipse(dotArea.toFloat());
    }
    content.removeFromRight(kGapPx);

    const auto stateColour = rowStateColour(row.state, textColour, mutedColour, warningColour);

    auto glyphArea =
        content.removeFromLeft(kStateGlyphDiameter).withSizeKeepingCentre(kStateGlyphDiameter, kStateGlyphDiameter);
    g.setColour(stateColour);
    g.fillEllipse(glyphArea.toFloat());
    content.removeFromLeft(kGapPx);

    g.setColour(stateColour);
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.drawText(rowDisplayName(row), content, juce::Justification::centredLeft, true);
}

// docs/control/midi-remote-ui.md#controllers-list-left: "(confirms with the count of project
// assignments it will orphan)" -- singular/plural, and a clean phrasing for the zero case rather
// than "will orphan 0 project assignments".
juce::String buildDeleteConfirmMessage(const juce::String& name, int assignmentCount) {
    if (assignmentCount <= 0)
        return "Delete '" + name + "'? It has no project assignments.";
    const juce::String noun = assignmentCount == 1 ? juce::String("assignment") : juce::String("assignments");
    return "Delete '" + name + "'? This will orphan " + juce::String(assignmentCount) + " project " + noun + ".";
}

// Right-click "Delete..." -- computes the orphan count via the owner's countProjectAssignments,
// then confirms via deleteConfirmHookForTest (a test) or a real juce::AlertWindow (production)
// before calling onDeleteConfirmed. Only touches ControllersListComponent's PUBLIC members (the
// callback std::functions), so it can live as a free function despite rows_ itself being private
// -- see the file comment on why this isn't a class member.
void beginDelete(ControllersListComponent& self, const juce::String& profileId, const juce::String& name) {
    const int count = self.countProjectAssignments ? self.countProjectAssignments(profileId) : 0;
    const juce::String message = buildDeleteConfirmMessage(name, count);

    juce::Component::SafePointer<ControllersListComponent> safeThis(&self);
    auto onChoice = [safeThis, profileId](int result) {
        auto* comp = safeThis.getComponent();
        if (comp == nullptr || result != 1)
            return;
        if (comp->onDeleteConfirmed)
            comp->onDeleteConfirmed(profileId);
    };

    if (auto& hook = test_hooks::deleteConfirmHookForTest())
        hook(message, onChoice);
    else
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                         .withTitle("Delete Controller")
                                         .withMessage(message)
                                         .withButton("Delete")
                                         .withButton("Cancel")
                                         .withAssociatedComponent(&self),
                                     onChoice);
}

// Right-click "Rename" -- prefilled text prompt (renamePromptHookForTest in a test, a real
// juce::AlertWindow text editor otherwise, mirroring GraphEditor::promptRenameMacro exactly). Never
// fires onRenameRequested for an empty or unchanged name.
void beginRename(ControllersListComponent& self, const juce::String& profileId, const juce::String& currentName) {
    juce::Component::SafePointer<ControllersListComponent> safeThis(&self);
    auto onChoice = [safeThis, profileId, currentName](const juce::String& typed) {
        auto* comp = safeThis.getComponent();
        const auto trimmed = typed.trim();
        if (comp == nullptr || trimmed.isEmpty() || trimmed == currentName)
            return;
        if (comp->onRenameRequested)
            comp->onRenameRequested(profileId, trimmed);
    };

    if (auto& hook = test_hooks::renamePromptHookForTest()) {
        hook(currentName, onChoice);
        return;
    }

    auto* window = new juce::AlertWindow("Rename Controller", "New name:", juce::AlertWindow::NoIcon);
    window->addTextEditor("name", currentName, "Controller name:");
    window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true, juce::ModalCallbackFunction::create([window, onChoice](int result) {
                                std::unique_ptr<juce::AlertWindow> owned(window);
                                if (result == 1)
                                    onChoice(owned->getTextEditorContents("name"));
                            }),
                            false);
}

// FRO139 (docs/control/midi-remote.md#controller-feedback): right-click "Send feedback to ->" --
// "None" plus one item per available output device, ticked against the row's own
// hasFeedbackOutput/feedbackOutputIdentifier. The device rows come from
// ControllersListComponent::queryFeedbackOutputs, called fresh on every right-click (the menu is
// short-lived and rebuilt each time, unlike the plugin-picker snapshot rule in Source/UI/CLAUDE.md,
// which exists for a list a background scan can mutate WHILE the menu is open) -- this component
// never calls juce::MidiOutput::getAvailableDevices() itself, see the header's own comment on why.
void appendFeedbackOutputSubmenu(ControllersListComponent& self, juce::PopupMenu& parent,
                                 const ControllersListComponent::RowModel& row) {
    juce::PopupMenu submenu;
    juce::Component::SafePointer<ControllersListComponent> safeThis(&self);
    const juce::String profileId = row.profileId;

    submenu.addItem("None", true, !row.hasFeedbackOutput, [safeThis, profileId] {
        auto* self2 = safeThis.getComponent();
        if (self2 != nullptr && self2->onFeedbackOutputRequested)
            self2->onFeedbackOutputRequested(profileId, {}, {});
    });
    submenu.addSeparator();
    const auto devices = self.queryFeedbackOutputs ? self.queryFeedbackOutputs()
                                                   : std::vector<ControllersListComponent::FeedbackDeviceOption>();
    for (const auto& device : devices) {
        const bool ticked = row.hasFeedbackOutput && row.feedbackOutputIdentifier == device.identifier;
        submenu.addItem(device.name, true, ticked,
                        [safeThis, profileId, identifier = device.identifier, name = device.name] {
                            auto* self2 = safeThis.getComponent();
                            if (self2 != nullptr && self2->onFeedbackOutputRequested)
                                self2->onFeedbackOutputRequested(profileId, identifier, name);
                        });
    }

    parent.addSubMenu("Send feedback to", submenu);
}

} // namespace

ControllersListComponent::ControllersListComponent() {
    addControllerButton_.setComponentID("addControllerButton");
    addControllerButton_.onClick = [this] {
        if (onAddControllerRequested)
            onAddControllerRequested(addControllerButton_);
    };
    addAndMakeVisible(addControllerButton_);
}
ControllersListComponent::~ControllersListComponent() = default;

void ControllersListComponent::setRows(const std::vector<RowModel>& rows) {
    std::vector<Row> updated;
    updated.reserve(rows.size());
    for (const auto& model : rows) {
        Row row;
        static_cast<RowModel&>(row) = model;
        // Preserve activity-lit state across a structural rebuild (setRows) for a profile that's
        // still present -- only setActivityLit's rising/falling-edge caller should clear it.
        auto it =
            std::find_if(rows_.begin(), rows_.end(), [&](const Row& r) { return r.profileId == model.profileId; });
        row.activityLit = it != rows_.end() && it->activityLit;
        updated.push_back(std::move(row));
    }
    rows_ = std::move(updated);

    // Header contract: "Preserves the current selection if `rows` still contains it."
    const bool selectionStillPresent =
        std::any_of(rows_.begin(), rows_.end(), [&](const Row& r) { return r.profileId == selectedProfileId_; });
    if (!selectionStillPresent)
        selectedProfileId_.clear();

    repaint();
}

void ControllersListComponent::setActivityLit(const juce::String& profileId, bool lit) {
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].profileId != profileId)
            continue;
        if (rows_[i].activityLit != lit) {
            rows_[i].activityLit = lit;
            // Source/UI/CLAUDE.md's no-unconditional-repaint rule: only this one row's bounds.
            repaint(boundsForRow(static_cast<int>(i)));
        }
        return;
    }
}

void ControllersListComponent::setSelectedProfileId(const juce::String& profileId) {
    if (selectedProfileId_ == profileId)
        return;
    selectedProfileId_ = profileId;
    repaint();
}

void ControllersListComponent::paint(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour textColour = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;
    const juce::Colour mutedColour = lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::grey;
    const juce::Colour warningColour = lf != nullptr ? lf->getTheme().colors.warning : juce::Colours::orange;
    const juce::Colour accentColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colours::cyan;

    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        const auto& row = rows_[static_cast<size_t>(i)];
        const bool selected = selectedProfileId_.isNotEmpty() && row.profileId == selectedProfileId_;
        paintRow(g, boundsForRow(i), row, row.activityLit, selected, textColour, mutedColour, warningColour,
                 accentColour);
    }
}

// Rows are painted directly from rows_ in paint() and boundsForRow()/rowIndexAt() derive their
// geometry on demand, so the only child to lay out is the FRO134 "+ Add controller" footer.
void ControllersListComponent::resized() {
    addControllerButton_.setBounds(getLocalBounds().removeFromBottom(kFooterHeight).reduced(8, 4));
}

void ControllersListComponent::setAddControllerVisible(bool visible) { addControllerButton_.setVisible(visible); }

void ControllersListComponent::setHosted(bool hosted) {
    hosted_ = hosted;
    setAddControllerVisible(!hosted);
}

// docs/control/midi-remote-ui.md#plugin-build: the panel says a standalone assignment does not fire
// inside a host, on the rows that would otherwise look live.
juce::String ControllersListComponent::getTooltipForProfile(const juce::String& profileId) const {
    const auto it = std::find_if(rows_.begin(), rows_.end(), [&](const Row& r) { return r.profileId == profileId; });
    if (it == rows_.end())
        return {};
    if (it->state == RowState::standaloneOnly)
        return "A controller from the standalone app. Assignments made against a real controller do not fire "
               "inside a host; only Host MIDI is live here.";
    if (it->state == RowState::orphan)
        return hosted_ ? "This project was made with a controller that is not set up on this machine. "
                         "A standalone assignment does not fire inside a host either way."
                       : "This project was made with a controller that is not set up on this machine.";
    return {};
}

juce::String ControllersListComponent::getTooltip() {
    const int index = rowIndexAt(getMouseXYRelative());
    return index < 0 ? juce::String() : getTooltipForProfile(rows_[static_cast<size_t>(index)].profileId);
}

juce::String ControllersListComponent::getRowDisplayNameForTest(const juce::String& profileId) const {
    const auto it = std::find_if(rows_.begin(), rows_.end(), [&](const Row& r) { return r.profileId == profileId; });
    return it == rows_.end() ? juce::String() : rowDisplayName(*it);
}

void ControllersListComponent::showContextMenuForRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= static_cast<int>(rows_.size()))
        return;

    const juce::String profileId = rows_[static_cast<size_t>(rowIndex)].profileId;
    const juce::String name = rows_[static_cast<size_t>(rowIndex)].name;

    // SafePointer, not a raw `this` capture -- an item's action() can run well after this call
    // returns (a real menu is asynchronous), and the dock/panel can be torn down while it's open.
    juce::Component::SafePointer<ControllersListComponent> safeThis(this);

    juce::PopupMenu menu;
    menu.addItem("Rename", [safeThis, profileId, name] {
        if (auto* self = safeThis.getComponent())
            beginRename(*self, profileId, name);
    });
    menu.addItem("Export...", [safeThis, profileId] {
        auto* self = safeThis.getComponent();
        if (self != nullptr && self->onExportRequested)
            self->onExportRequested(profileId);
    });
    menu.addItem("Delete...", [safeThis, profileId, name] {
        if (auto* self = safeThis.getComponent())
            beginDelete(*self, profileId, name);
    });

    // FRO139 (docs/control/midi-remote.md#controller-feedback): hidden in the plugin build, same as
    // "+ Add controller" -- a hosted plugin has no MIDI output of its own to pick.
    if (!hosted_)
        appendFeedbackOutputSubmenu(*this, menu, rows_[static_cast<size_t>(rowIndex)]);

    // Bare Options() -- menu at the mouse position, desktop-level (ModuleComponent's own default
    // hook uses the same). A withParentComponent(this) would parent the popup INSIDE the 240px-wide
    // list instead, clipping it at the dock edge for any row near the bottom.
    if (auto& hook = test_hooks::contextMenuHookForTest())
        hook(menu);
    else
        menu.showMenuAsync(juce::PopupMenu::Options());
}

juce::Rectangle<int> ControllersListComponent::boundsForRow(int rowIndex) const {
    return {0, rowIndex * kRowHeight, getWidth(), kRowHeight};
}

int ControllersListComponent::rowIndexAt(juce::Point<int> position) const {
    if (position.x < 0 || position.x >= getWidth() || position.y < 0)
        return -1;
    const int index = position.y / kRowHeight;
    return index >= 0 && index < static_cast<int>(rows_.size()) ? index : -1;
}

void ControllersListComponent::mouseDown(const juce::MouseEvent& event) {
    const int rowIndex = rowIndexAt(event.getPosition());
    if (rowIndex < 0)
        return;

    if (event.mods.isPopupMenu()) {
        showContextMenuForRow(rowIndex);
        return;
    }

    const juce::String profileId = rows_[static_cast<size_t>(rowIndex)].profileId;
    setSelectedProfileId(profileId); // orphan rows are still selectable -- see header's RowState
    if (onSelectProfile)
        onSelectProfile(profileId);
}

} // namespace synth::ui
