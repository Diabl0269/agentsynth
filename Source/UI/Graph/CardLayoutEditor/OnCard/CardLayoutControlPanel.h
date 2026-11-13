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

class ModCurveBox;
class ModCurveEditTarget;

class CardLayoutControlPanel final
    : public juce::Component
    , private juce::Timer {
public:
    CardLayoutControlPanel();
    ~CardLayoutControlPanel() override;

    /** A Macros knob: shows the small curve box for `target` (null removes it). Its Enlarge runs `onEnlargeCurve`, Esc
     *  inside it closes the panel. */
    void setCurveTarget(std::shared_ptr<ModCurveEditTarget> target);
    ModCurveBox* getCurveBoxForTest() const noexcept;
    std::function<void()> onEnlargeCurve;

    /** Shows `options`: rows appear or go as they apply, fields take their values, the panel resizes. */
    void setOptions(const ControlOptions& options);
    const ControlOptions& getOptions() const noexcept { return options_; }

    std::function<void(CardWidget)> onShowAs;
    std::function<void(const juce::String& text)> onLabel;
    /** A Macros knob's Name field was committed. */
    std::function<void(const juce::String& text)> onMacroName;
    /** A Macros knob's Polarity switch was picked: true = Bi. */
    std::function<void(bool bipolar)> onMacroPolarity;
    /** An XY pad's Y macro was picked: the macro's parameter id. */
    std::function<void(const juce::String& paramId)> onXYPartner;
    /** A Button's Mode switch was picked: true = Momentary. */
    std::function<void(bool momentary)> onButtonMode;
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
    CardSegmentedSwitch* getFaderSizeForTest() const noexcept { return faderSize_.get(); }
    bool isFaderSizeRowShownForTest() const { return faderSizeFade_ != nullptr && faderSizeFade_->isShown(); }
    bool isSizeRowShownForTest() const { return sizeFade_ != nullptr && sizeFade_->isShown(); }
    bool isDirectionRowShownForTest() const { return directionFade_ != nullptr && directionFade_->isShown(); }
    juce::TextEditor& getLabelEditorForTest() noexcept { return label_; }
    juce::TextEditor& getNameEditorForTest() noexcept { return name_; }
    CardSegmentedSwitch* getPolarityForTest() const noexcept { return polarity_.get(); }
    juce::ComboBox& getYMacroForTest() noexcept { return yMacro_; }
    CardSegmentedSwitch* getModeForTest() const noexcept { return mode_.get(); }
    bool isModeRowShownForTest() const { return modeFade_ != nullptr && modeFade_->isShown(); }
    bool isYMacroRowShownForTest() const { return yMacroFade_.isShown(); }
    bool isMacroRowsShownForTest() const {
        return nameFade_.isShown() && polarityFade_ != nullptr && polarityFade_->isShown();
    }
    bool isLabelRowShownForTest() const { return labelFade_.isShown(); }
    void commitNameForTest() { commitName(); }
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
    void commitName();
    void commitRange();
    void fillFields();
    int arrange();
    int preferredWidth() const;
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
    std::unique_ptr<CardSegmentedSwitch> faderSize_; ///< Short | Long, for a vertical fader.
    juce::Label sizeCaption_{{}, "Size"};
    juce::Label faderSizeCaption_{{}, "Size"};
    juce::Label directionCaption_{{}, "Direction"};
    juce::Label showAsCaption_{{}, "Show as"};
    juce::Label labelCaption_{{}, "Label"};
    juce::Label nameCaption_{{}, "Name"};
    juce::Label polarityCaption_{{}, "Polarity"};
    juce::Label yMacroCaption_{{}, "Y macro"};
    juce::Label modeCaption_{{}, "Mode"};
    juce::Label rangeCaption_{{}, "Range"};
    juce::Label rangeTo_{{}, "to"};
    juce::Label hint_;
    juce::TextEditor label_;
    juce::ComboBox yMacro_;                         ///< An XY pad's Y macro.
    juce::TextEditor name_;                         ///< A Macros knob's name (in place of Label).
    std::unique_ptr<CardSegmentedSwitch> polarity_; ///< A Macros knob's Uni | Bi.
    std::unique_ptr<CardSegmentedSwitch> mode_;     ///< A Button's Toggle | Momentary.
    juce::TextEditor minimum_;
    juce::TextEditor maximum_;
    juce::TextButton hide_{"Hide from card"};
    // Rows that come and go fade (and the panel's height follows their progress): FadeVisibility, never setVisible.
    // The Size, Direction and fader Size rows are made with their switches.
    std::unique_ptr<synth::ui::FadeVisibility> sizeFade_;
    std::unique_ptr<synth::ui::FadeVisibility> directionFade_;
    std::unique_ptr<synth::ui::FadeVisibility> faderSizeFade_;
    std::unique_ptr<synth::ui::FadeVisibility> polarityFade_; ///< Caption and switch, made with the switch.
    std::unique_ptr<synth::ui::FadeVisibility> modeFade_;     ///< Caption and switch, made with the switch.
    synth::ui::FadeVisibility labelFade_{&labelCaption_, &label_};
    synth::ui::FadeVisibility nameFade_{&nameCaption_, &name_};
    synth::ui::FadeVisibility yMacroFade_{&yMacroCaption_, &yMacro_};
    synth::ui::FadeVisibility rangeFade_{&rangeCaption_, &rangeTo_, &minimum_, &maximum_};
    synth::ui::FadeVisibility hintFade_{&hint_};
    std::unique_ptr<ModCurveBox> curveBox_; ///< A Macros knob's response curve.
    std::unique_ptr<synth::ui::FadeVisibility> curveFade_;
    int focusTries_ = 0;
    int showAsWidth_ = 0; ///< The switch's width for its segment texts.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardLayoutControlPanel)
};

} // namespace synth::ui
