#include "ControlStylePanel.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/CardBody/ControlStyleNodeOverride.h"
#include "UI/Graph/ControlStyle/ControlStyleCards.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kPad = 14;
constexpr int kTitleHeight = 24;
constexpr int kScopeHeight = 26;
constexpr int kScopeWidth = 190;
constexpr int kRadioGroup = 0x4373;
} // namespace

ControlStylePanel::ControlStylePanel(GraphEditor& editor, juce::AudioProcessorGraph::NodeID nodeId,
                                     const juce::String& typeName)
    : editor_(editor)
    , nodeId_(nodeId)
    , typeName_(typeName) {
    setSize(kWidth, kHeight);
    setWantsKeyboardFocus(false);
    title_.setInterceptsMouseClicks(false, false);
    title_.setFont(synth::theme::AppLookAndFeel::uiFont(15.0f));
    addAndMakeVisible(title_);

    typeButton_.setButtonText("Every " + typeName);
    for (auto* button : {&moduleButton_, &typeButton_}) {
        button->setRadioGroupId(kRadioGroup);
        button->setConnectedEdges(button == &moduleButton_ ? juce::Button::ConnectedOnRight
                                                           : juce::Button::ConnectedOnLeft);
        button->setClickingTogglesState(true);
        button->setWantsKeyboardFocus(true);
        button->setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(*button);
    }
    moduleButton_.setTitle("Control style for this module");
    moduleButton_.setTooltip("Choose the knob and fader style of this module alone");
    typeButton_.setTitle("Control style for every " + typeName);
    typeButton_.setTooltip("Choose the knob and fader style of every " + typeName + " module");
    moduleButton_.setToggleState(true, juce::dontSendNotification);
    moduleButton_.onClick = [this] {
        if (moduleButton_.getToggleState())
            setScope(Scope::Module);
    };
    typeButton_.onClick = [this] {
        if (typeButton_.getToggleState())
            setScope(Scope::Type);
    };
    typeButton_.setEnabled(userSettingsOf(editor_) != nullptr);

    addAndMakeVisible(picker_);
    picker_.onOverrideChanged = [this](auto knob, auto fader) { write(knob, fader); };
    refreshPicker();
    refreshTheme();
}

void ControlStylePanel::setScope(Scope scope) {
    scope_ = scope;
    (scope == Scope::Module ? moduleButton_ : typeButton_).setToggleState(true, juce::dontSendNotification);
    refreshPicker();
}

void ControlStylePanel::refreshPicker() {
    using synth::theme::ControlStyleOverride;
    ControlStyleOverride current;
    if (scope_ == Scope::Module) {
        picker_.setOverrideScope("this module", "Use the style set for every " + typeName_ +
                                                    ", else Settings > Appearance > Controls");
        current = getControlStyleNodeOverride(editor_.getAudioEngine().getGraph(), nodeId_);
    } else {
        picker_.setOverrideScope("every " + typeName_, "Use the style from Settings > Appearance > Controls");
        if (auto* settings = userSettingsOf(editor_))
            current = synth::theme::loadTypeControlStyle(*settings, typeName_);
    }
    picker_.setOverride(current.knob, current.fader);
}

// This module: one undo step covering the property write and the card refresh, so undo and redo repaint the card
// too. Every type: the user setting, flushed at once.
void ControlStylePanel::write(std::optional<synth::theme::KnobStyle> knob,
                              std::optional<synth::theme::KnobStyle> fader) {
    const synth::theme::ControlStyleOverride wanted{knob, fader};
    if (scope_ == Scope::Type) {
        if (auto* settings = userSettingsOf(editor_)) {
            synth::theme::writeTypeControlStyle(*settings, typeName_, wanted);
            settings->saveIfNeeded();
        }
        refreshAllControlStyles(editor_);
        return;
    }
    auto& graph = editor_.getAudioEngine().getGraph();
    if (wanted == synth::getControlStyleNodeOverride(graph, nodeId_))
        return;
    auto* editor = &editor_;
    const auto nodeId = nodeId_;
    auto apply = [editor, &graph, nodeId, wanted] {
        synth::setControlStyleNodeOverride(graph, nullptr, nodeId, wanted);
        synth::refreshAllControlStyles(*editor);
    };
    if (auto* undo = editor_.getUndoManager())
        undo->recordStructuralChange(graph, apply);
    else
        apply();
}

// The panel paints its own surface and hairline border (the shared Popover look), so it reads the same in every
// theme whichever window hosts it.
void ControlStylePanel::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(theme.colors.surface);
    g.fillRoundedRectangle(bounds, theme.metrics.cornerRadius);
    g.setColour(theme.colors.border);
    g.drawRoundedRectangle(bounds, theme.metrics.cornerRadius, 1.0f);
}

void ControlStylePanel::resized() {
    auto area = getLocalBounds().reduced(kPad);
    title_.setBounds(area.removeFromTop(kTitleHeight));
    area.removeFromTop(6);
    auto scopes = area.removeFromTop(kScopeHeight);
    moduleButton_.setBounds(scopes.removeFromLeft(kScopeWidth));
    typeButton_.setBounds(scopes.removeFromLeft(kScopeWidth));
    area.removeFromTop(10);
    picker_.setBounds(area.removeFromTop(KnobStylePicker::kOverrideHeight));
}

// Left/Right on a scope button picks the other scope; Escape closes the panel. A preview handles its own arrows.
bool ControlStylePanel::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey)
        return closeHostingWindow(*this);
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    const bool onScope = moduleButton_.hasKeyboardFocus(false) || typeButton_.hasKeyboardFocus(false);
    if (onScope && key == juce::KeyPress::rightKey && typeButton_.isEnabled()) {
        setScope(Scope::Type);
        typeButton_.grabKeyboardFocus();
        return true;
    }
    if (onScope && key == juce::KeyPress::leftKey) {
        setScope(Scope::Module);
        moduleButton_.grabKeyboardFocus();
        return true;
    }
    return false;
}

void ControlStylePanel::parentHierarchyChanged() { refreshTheme(); }
void ControlStylePanel::lookAndFeelChanged() { refreshTheme(); }

void ControlStylePanel::refreshTheme() {
    const auto& theme = synth::theme::themeOf(*this);
    title_.setColour(juce::Label::textColourId, theme.colors.textPrimary);
    picker_.setTheme(theme);
}

} // namespace synth::ui
