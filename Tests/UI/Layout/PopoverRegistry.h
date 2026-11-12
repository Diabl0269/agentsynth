// PopoverRegistry.h -- every Source file that opens a popover through launchPopover or owns a PopoverSlot, with the
// content classes it puts in the box. PopupOwnPathTests scans Source against it (a new opener, or one that stopped
// opening popovers, fails by name); PopupHostBehaviourTests builds each content class named here and checks its
// open, toggle and close frames.
#pragma once

#include <vector>

namespace synth::test {

struct PopoverUser {
    const char* file;
    std::vector<const char*> contents;
};

inline const std::vector<PopoverUser>& popoverUsers() {
    static const std::vector<PopoverUser> users = {
        {"Source/UI/Chrome/ColourPickerPopupShow.cpp", {"ColourPickerPopup"}},
        {"Source/UI/DrumMachine/DrumMachineIdeas.cpp", {"DrumIdeasPanel"}},
        {"Source/UI/Graph/CardLayoutEditor/OnCard/CardLayoutOnCardEditorPanel.cpp", {"CardLayoutControlPanel"}},
        {"Source/UI/Graph/ControlStyle/ControlStyleMenu.cpp", {"ControlStylePanel"}},
        {"Source/UI/Graph/GraphEditor/GraphEditorMacroPrompts.cpp", {"ColourPickerPopup"}},
        {"Source/UI/Graph/ModMatrixComponent.cpp", {"ModMatrixPicker"}},
        {"Source/UI/Graph/ModuleComponent/ModuleComponentLayoutEditor.cpp", {"CardLayoutOnCardEditor"}},
        {"Source/UI/Graph/ModuleComponent/ReplaceWithPicker.cpp", {"ModMatrixPicker"}},
        {"Source/UI/Graph/ModuleComponent/SamplerChrome/InstrumentPickerRow.cpp", {"ModMatrixPicker"}},
        {"Source/UI/Graph/PluginKnobPicker/HostedModTargetPicker.cpp", {"ModMatrixPicker"}},
        {"Source/UI/Layout/FollowKeyField.cpp", {"KeyPicker"}},
        {"Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogInternal.h", {"ColourPickerPopup"}},
        {"Source/UI/MidiRemote/MidiRemotePanel/MidiRemotePanelAssign.cpp", {"ActionPickerComponent"}},
        {"Source/UI/MidiRemote/MidiRemotePanel/MidiRemotePanelControllers.cpp", {"AddControllerPopover"}},
        {"Source/UI/Mixer/MixerInsertList/MixerInsertListEdits.cpp", {"ModMatrixPicker"}},
        {"Source/UI/Mixer/MixerPanelComponent/MixerPanelColour.cpp", {"ColourPickerPopup"}},
        {"Source/UI/ModuleViews/EQCurveTypeMenu.cpp", {"EQBandTypeMenu"}},
        {"Source/UI/ModuleViews/MorphMap/MorphMapKeyboard.cpp", {"ColourPickerPopup"}},
        {"Source/UI/ModuleViews/ShapeEditor/ShapeListEditorDraw.cpp", {"DrawShapeFlyout"}},
        {"Source/UI/ModuleViews/ShapeEditor/ShapeListEditorRing.cpp", {"ShapeStackPanel"}},
        {"Source/UI/ModuleViews/StepGrid/StepGridChordActions.cpp", {"StepGridChordPicker"}},
        {"Source/UI/ModuleViews/StepGrid/StepGridComponent.h", {"StepGridRandomizePanel"}},
        {"Source/UI/ModuleViews/StepGrid/StepGridToolActions.cpp", {"StepGridRandomizePanel"}},
        {"Source/UI/PianoRoll/ScaleAssistPanel.cpp", {"KeyPicker"}},
        {"Source/UI/Settings/AccountSettingsTab.cpp", {"AccountFlowPanel"}},
        {"Source/UI/Settings/AppearanceSettingsTab.cpp", {"juce::ColourSelector"}},
        {"Source/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabLifecycle.cpp",
         {"DualIOPerModulePopupContent"}},
        {"Source/UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderMenu.cpp", {"ModMatrixPicker"}},
        {"Source/UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderParameterPicker.cpp",
         {"ModMatrixPicker"}},
        {"Source/UI/Timeline/AutomationLanes/Modulators/ModulatorRowEdits.cpp", {"ModMatrixPicker"}},
        {"Source/UI/Timeline/SongKeyField.cpp", {"KeyPicker"}},
        {"Source/UI/Timeline/TimelinePanelComponent/AddTrackPicker.cpp", {"ModMatrixPicker"}},
        {"Source/UI/Timeline/TimelinePanelComponent/TimelinePanelAddAutomation.cpp", {"ModMatrixPicker"}},
        {"Source/UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h", {"DrawShapeFlyout"}},
        {"Source/UI/Timeline/TimelinePanelComponent/TimelinePanelShapes.cpp", {"DrawShapeFlyout"}},
        {"Source/UI/Timeline/TimelineRoutingPane/TimelineRoutingPaneActions.cpp", {"MidiDestinationPicker"}},
        {"Source/UI/Timeline/TimelineRulerComponent.cpp", {"ColourPickerPopup"}},
        {"Source/UI/Timeline/TimelineSectionStrip/TimelineSectionStripMenu.cpp", {"ColourPickerPopup"}},
        {"Source/UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.cpp",
         {"ColourPickerPopup", "MidiDestinationPicker"}},
    };
    return users;
}

/** A hover label that is painted, not a window: it draws the shared tooltip box (`paintTooltipBox` / `drawTooltip`)
 *  itself, so it has no PopupHost or PopupMotion to fade it. `fadeOwner` is the token its file must name: the one
 *  object that owns its alpha (ShownFade, or a class built on it), so it rises on show and falls on hide. */
struct HoverLabelPainter {
    const char* file;
    const char* fadeOwner;
};

inline const std::vector<HoverLabelPainter>& hoverLabelPainters() {
    static const std::vector<HoverLabelPainter> painters = {
        // The chip under a knob, painted in ModuleComponent::paintOverChildren; ModDotTooltip's fade rides along.
        {"Source/UI/Graph/ModuleComponent/ModuleComponentModChip.cpp", "ModChipFade"},
        // The tooltip window: its own window, faded by PopupMotion.
        {"Source/UI/Layout/AppTooltipWindow.cpp", "beginFadeIn"},
    };
    return painters;
}

} // namespace synth::test
