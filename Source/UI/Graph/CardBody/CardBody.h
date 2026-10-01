#pragma once

#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardBody/CardBodyPlan.h"
#include <memory>

class ModuleComponent;
class ThresholdControlComponent;

namespace synth {

class CardMoreButton;
class ModuleCardLayoutStore;

/**
 * Owns the parameter widgets, captions, attachments and views of a built-in module's card body: it
 * builds one widget per plan item (in parameter declaration order, so child and Tab order are the
 * generic card's), keeps every binding (attachment, MIDI Learn, modulation-amount gesture, knob-bound
 * jack) the card always had, and lays the body out with one measure-or-apply function. Widgets are
 * children of the card itself. docs/layout/module-card-layout.md#rendering.
 */
class CardBody
    : private juce::AudioProcessorParameter::Listener
    , private juce::AsyncUpdater {
public:
    /** `card` owns this and outlives it; `layout` nullopt = the automatic layout; `dimRules` are the
     *  type's code dim rules (applied only with a layout). */
    CardBody(ModuleComponent& card, juce::AudioProcessor& module, const std::optional<CardLayout>& layout,
             const std::vector<CardDimRule>& dimRules = {});
    /** The body for `module`'s card, its layout resolved for node `nodeId` against `store` (may be
     *  null); null when the card builds its own widgets (cardBodyBuildsWidgetsFor). Message thread only. */
    static std::unique_ptr<CardBody> createFor(ModuleComponent& card, juce::AudioProcessor& module,
                                               const juce::AudioProcessorGraph& graph,
                                               juce::AudioProcessorGraph::NodeID nodeId,
                                               ModuleCardLayoutStore* store = nullptr);
    ~CardBody() override;

    // ---- Building (the card's constructor, in this order) --------------------------------------
    /** Builds the view items; call where the card has always built its Threshold control. */
    void createViews();
    /** Builds every parameter widget and binds it; call from the card's createControls(). */
    void createParameterWidgets();

    // ---- Layout ----------------------------------------------------------------------------------
    /** Lays the sections out from `y`; returns the y below them. `tabbed` skips combos and knobs. */
    int layout(int y, const cardbody::BodyGeometry& g, bool apply, bool tabbed) const;
    /** Lays out the More row (a no-op without hidden parameters); returns the y below it. */
    int layoutMoreRow(int y, const cardbody::BodyGeometry& g, bool apply) const;
    /** True when the layout has a footer section; the card's chrome toggles then join that row. */
    bool hasFooter() const { return plan_.hasFooter(); }
    /** Lays out the footer row: its items, then `chromeToggles` (null entries skipped) as pills. */
    int layoutFooter(int y, const cardbody::BodyGeometry& g, bool apply,
                     const std::vector<juce::ToggleButton*>& chromeToggles) const;

    // ---- Conditions ------------------------------------------------------------------------------
    /** True for a swap-group member out of view only because a sibling is shown in its cell. */
    bool isSwappedOut(const juce::Component& widget) const;
    /** Re-reads every condition now; on a change, re-lays out the card (and makes room if it grew). */
    void refreshConditions();
    /** Adds the dimmed hint to dimmed controls' tooltip and description; call once the card has named
     *  its controls (their tooltips are set after the body is built). */
    void applyDimHints();
    /** Runs a condition re-read a parameter change queued, if any, now. Message thread only. */
    void flushPendingConditionUpdate() { handleUpdateNowIfNeeded(); }

    // ---- Lookup ----------------------------------------------------------------------------------
    /** The widget bound to `paramId`, or null. */
    juce::Component* findWidget(const juce::String& paramId) const;
    /** The Threshold view, or null. */
    ThresholdControlComponent* getThresholdView() const;
    const CardBodyPlan& getPlan() const { return plan_; }
    /** The layout this body draws as explicit items: the resolved layout, or the automatic one
     *  written out (the same card when built from it). */
    CardLayout explicitLayout() const;
    /** False for the bespoke cards, which always build from the automatic plan. */
    bool drawsFromLayout() const;
    /** The node's "cardLayout" JSON this body was built from; empty for none. */
    const juce::String& builtFromOverride() const noexcept { return builtFromOverride_; }
    /** True when `node`'s override or its type's stored default changed since this body was built. */
    bool isStaleFor(const juce::AudioProcessorGraph::Node& node) const;

    // ---- The More row ----------------------------------------------------------------------------
    bool hasMoreRow() const { return !plan_.more.empty(); }
    bool isMoreUnfolded() const { return moreUnfolded_; }
    /** Shows or hides the More row's widgets and resizes the card (make room / give it back). */
    void setMoreUnfolded(bool unfolded);
    /** Null without a More row. */
    juce::Button* getMoreButton() const;
    /** Unfolds the More row when a cable dragged over the card is at `cardLocal`; true if it did. */
    bool unfoldForCableDragAt(juce::Point<int> cardLocal);

    // ---- Teardown (the card's detachFromProcessor) -------------------------------------------------
    /** Destroys the views; they time against the module. */
    void releaseViews();
    /** Destroys the attachments, or leaks them when `processorAlive` is false. */
    void releaseBindings(bool processorAlive);

private:
    void createChoice(CardBodyItem& item, juce::AudioParameterChoice& param);
    void createKnob(CardBodyItem& item, juce::RangedAudioParameter& param);
    void createToggle(CardBodyItem& item, juce::AudioParameterBool& param);
    void createFader(CardBodyItem& item, juce::RangedAudioParameter& param);
    void createSegmented(CardBodyItem& item, juce::AudioParameterChoice& param);
    void createStepper(CardBodyItem& item, juce::AudioParameterInt& param);
    juce::Label* addCaption(CardBodyItem& item, juce::RangedAudioParameter& param, juce::Justification justification);
    void createSectionHeaders();
    void createMoreButton();
    void applyVisibility();
    void styleFooterItems();
    void startWatchingConditions();
    void stopWatchingConditions(bool processorAlive);
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;
    int layoutItems(const std::vector<int>& indices, int columns, int y, const cardbody::BodyGeometry& g, bool apply,
                    bool tabbed) const;

    ModuleComponent& card_;
    juce::AudioProcessor& module_;
    CardBodyPlan plan_;
    std::optional<CardLayout> layout_;
    juce::String builtFromOverride_;
    juce::WeakReference<ModuleCardLayoutStore> store_;
    int builtFromRevision_ = 0;
    bool moreUnfolded_ = false;
    bool dimHintsReady_ = false;
    std::vector<juce::RangedAudioParameter*> watched_; ///< Parameters this body listens to; empty once released.

    // Widgets before attachments: members unwind in reverse, so an attachment never outlives its widget.
    juce::OwnedArray<juce::Component> widgets_;
    juce::OwnedArray<juce::Component> views_;
    std::unique_ptr<CardMoreButton> moreButton_;
    juce::OwnedArray<juce::SliderParameterAttachment> sliderAttachments_;
    juce::OwnedArray<juce::ComboBoxParameterAttachment> comboAttachments_;
    juce::OwnedArray<juce::ButtonParameterAttachment> buttonAttachments_;
    juce::OwnedArray<juce::ParameterAttachment> paramAttachments_; ///< Segmented switches and steppers.

    JUCE_DECLARE_NON_COPYABLE(CardBody)
};

} // namespace synth
