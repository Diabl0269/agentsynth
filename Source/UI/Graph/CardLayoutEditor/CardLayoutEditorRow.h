#pragma once

#include "UI/Graph/CardLayoutEditor/CardLayoutEditorModel.h"
#include "UI/Layout/NonModalLabel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** A row's grab handle: three short bars that show the grab hand on hover and during a drag, and
 *  report the press/drag/release, whole, to the row (which forwards them to its owner). */
class CardLayoutEditorGrip final : public juce::Component {
public:
    CardLayoutEditorGrip();

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    std::function<void(const juce::MouseEvent&)> onPress;
    std::function<void(const juce::MouseEvent&)> onMove;
    std::function<void()> onRelease;

private:
    bool pressed_ = false;
};

/**
 * One row of the card layout editor's list: a group header (its title, click to rename) or a control
 * (tick = shown on the card, grab handle, name = click to rename, widget choice). The row is the
 * keyboard focus stop; its keys go to the owner through onKey. Pure UI: the owner decides what every
 * report means. docs/layout/module-card-layout.md#editing-a-layout.
 */
class CardLayoutEditorRow final
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    /** `draggable`: shows the grab handle. `renameable`: a click on the name edits it. */
    CardLayoutEditorRow(const CardLayoutEditorModel::Row& row, bool draggable, bool renameable);

    const juce::String& getKey() const noexcept { return key_; }
    bool isHeader() const noexcept { return header_; }
    bool isChecked() const noexcept { return tick_.getToggleState(); }
    bool isDraggable() const noexcept { return draggable_; }
    /** The rename override shown; nullopt = the control's own name. */
    const std::optional<juce::String>& getLabelOverride() const noexcept { return labelOverride_; }

    /** 0..1: how strongly the row is drawn lifted while it is dragged. */
    void setLift(float lift);
    /** Opens the name for editing (Enter). False when the row cannot be renamed. */
    bool startRename();
    /** Flips the tick and reports it (Space, or a click on the tick). False for a header. */
    bool toggleChecked();

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

    std::function<void(bool checked)> onToggled;
    /** The name was edited; the text is trimmed (empty = the control's own name / no group title). */
    std::function<void(const juce::String& text)> onRenamed;
    std::function<void(CardWidget widget)> onWidgetChosen;
    /** A key the row does not handle itself; true when the owner consumed it. */
    std::function<bool(const juce::KeyPress& key)> onKey;
    /** Drag on the grab handle. The events arrive whole (the row moves under the pointer), and the row
     *  may be destroyed by onDragEnded. */
    std::function<void(const juce::MouseEvent&)> onDragStarted;
    std::function<void(const juce::MouseEvent&)> onDragUpdated;
    std::function<void()> onDragEnded;

    // ---- Test seams: drive the real controls -------------------------------------------------------
    void triggerToggleForTest() { toggleChecked(); }
    void setLabelTextForTest(const juce::String& text) { pendingLabelForTest_ = text; }
    void commitLabelForTest() { nameLabel_.setText(pendingLabelForTest_, juce::sendNotificationSync); }
    juce::Component& getDragHandleForTest() noexcept { return grip_; }
    juce::ComboBox& getWidgetComboForTest() noexcept { return widgetCombo_; }
    juce::Label& getNameLabelForTest() noexcept { return nameLabel_; }

    static constexpr int kRowHeight = 26;

private:
    void buildTick(const CardLayoutEditorModel::Row& row);
    void buildName(bool renameable);
    void buildWidgetChoice(const CardLayoutEditorModel::Row& row);
    void buildGrip();
    void commitName(const juce::String& text);
    juce::String shownName() const;

    juce::String key_;
    bool header_ = false;
    bool tab_ = false; ///< A header of a tab section.
    bool draggable_ = false;
    juce::String name_;         ///< The control's own name, or the group title.
    juce::String fallbackName_; ///< A group's name while it has no title.
    std::optional<juce::String> labelOverride_;
    std::vector<CardWidget> widgetChoices_;
    juce::String pendingLabelForTest_;

    juce::ToggleButton tick_;
    NonModalLabel nameLabel_;
    juce::ComboBox widgetCombo_;
    CardLayoutEditorGrip grip_;
    float lift_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardLayoutEditorRow)
};

} // namespace synth::ui
