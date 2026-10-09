#pragma once

// The on-card editor's per-control panel: every option of one control in one small panel (Show as, Size or
// Direction, Label, Range, Hide from card). It lives in a call-out the editor launches and holds no layout of its own:
// each change goes out through a callback, and the editor hands back the control's new state.
// docs/layout/module-card-layout.md#editing-a-layout.

#include "OnCardControlOptions.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Layout/FadeVisibility.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

class CardLayoutControlPanel final
    : public juce::Component
    , private juce::Timer {
public:
    CardLayoutControlPanel();

    /** Shows `options`: rows appear or go as they apply, fields take their values, the panel resizes. */
    void setOptions(const ControlOptions& options);
    const ControlOptions& getOptions() const noexcept { return options_; }

    std::function<void(CardWidget)> onShowAs;
    std::function<void(const juce::String& text)> onLabel;
    /** A valid range was typed; nullopt = the full range. */
    std::function<void(std::optional<juce::Range<double>>)> onRange;
    std::function<void()> onHide;
    std::function<void()> onRequestClose;

    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void lookAndFeelChanged() override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

    // ---- Test seams ---------------------------------------------------------------------------------
    CardSegmentedSwitch* getShowAsForTest() const noexcept { return showAs_.get(); }
    CardSegmentedSwitch* getSizeForTest() const noexcept { return size_.get(); }
    CardSegmentedSwitch* getDirectionForTest() const noexcept { return direction_.get(); }
    bool isSizeRowShownForTest() const { return sizeFade_ != nullptr && sizeFade_->isShown(); }
    bool isDirectionRowShownForTest() const { return directionFade_ != nullptr && directionFade_->isShown(); }
    juce::TextEditor& getLabelEditorForTest() noexcept { return label_; }
    juce::TextEditor& getMinimumEditorForTest() noexcept { return minimum_; }
    juce::TextEditor& getMaximumEditorForTest() noexcept { return maximum_; }
    juce::TextButton& getHideButtonForTest() noexcept { return hide_; }
    juce::String getHintForTest() const { return hint_.getText(); }
    bool isRangeRowShownForTest() const { return rangeFade_.isShown(); }
    bool isHintShownForTest() const { return hintFade_.isShown(); }
    /** What Return in the field does. */
    void commitLabelForTest() { commitLabel(); }
    void commitRangeForTest() { commitRange(); }

private:
    void buildShowAs();
    void buildPairSwitch(std::unique_ptr<CardSegmentedSwitch>& target, const juce::String& title,
                         const juce::String& tooltip, const juce::StringArray& values, int focusOrder, CardWidget first,
                         CardWidget second);
    void styleEditor(juce::TextEditor& editor, const juce::String& title, const juce::String& tooltip);
    void commitLabel();
    void commitRange();
    void fillFields();
    int arrange();
    static int scaled(int value, float progress);
    static void squeeze(juce::Component& c, int x, int top, int w, int offset, int height, int slotHeight);
    void applyColours();
    void timerCallback() override;
    bool focusIsInside() const;
    juce::Component* firstFocusStop() const;

    ControlOptions options_;
    std::vector<WidgetKind> kinds_; ///< What each Show as segment stands for.
    std::unique_ptr<CardSegmentedSwitch> showAs_;
    std::unique_ptr<CardSegmentedSwitch> size_;      ///< Small | Large, for a knob.
    std::unique_ptr<CardSegmentedSwitch> direction_; ///< Vertical | Horizontal, for a fader.
    juce::Label sizeCaption_{{}, "Size"};
    juce::Label directionCaption_{{}, "Direction"};
    juce::Label showAsCaption_{{}, "Show as"};
    juce::Label labelCaption_{{}, "Label"};
    juce::Label rangeCaption_{{}, "Range"};
    juce::Label rangeTo_{{}, "to"};
    juce::Label hint_;
    juce::TextEditor label_;
    juce::TextEditor minimum_;
    juce::TextEditor maximum_;
    juce::TextButton hide_{"Hide from card"};
    // Rows that come and go fade (and the panel's height follows their progress): FadeVisibility, never setVisible.
    // The Size and Direction rows are made with their switches.
    std::unique_ptr<synth::ui::FadeVisibility> sizeFade_;
    std::unique_ptr<synth::ui::FadeVisibility> directionFade_;
    synth::ui::FadeVisibility rangeFade_{&rangeCaption_, &rangeTo_, &minimum_, &maximum_};
    synth::ui::FadeVisibility hintFade_{&hint_};
    int focusTries_ = 0;
    int showAsWidth_ = 0; ///< The switch's width for its segment texts.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardLayoutControlPanel)
};

} // namespace synth::ui
