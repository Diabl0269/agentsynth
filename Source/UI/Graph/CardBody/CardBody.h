#pragma once

#include "UI/Graph/CardBody/CardBlockFade.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardBody/CardBodyPlan.h"
#include <memory>
#include <optional>
#include <set>

class ModuleComponent;
class ThresholdControlComponent;

namespace synth {

class CardMoreButton;
class ModuleCardLayoutStore;
class SwapMotion;

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
    /** Lays the sections out from `y`; returns the y below them. */
    int layout(int y, const cardbody::BodyGeometry& g, bool apply) const;
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
    /** Runs after a re-read changed what the card shows, and again once a swap's motion has landed (the
     *  on-card layout editor re-reads its outlines). */
    std::function<void()> onConditionsApplied;

    // ---- Swap motion (CardBodySwapMotion.cpp) ------------------------------------------------------
    /** A swap of controls in place (a Sync flip) shrinks the leaving ones, THEN grows the arriving ones; the
     *  card keeps its size and Reduce Motion swaps at once. Runs only while the card is showing. */
    bool isSwapMotionRunning() const;
    /** Lands a running swap motion at once. */
    void finishSwapMotion();
    /** True while `component` is an arriving control the swap motion keeps hidden. */
    bool isHeldBySwap(const juce::Component& component) const;
    /** Test seams: run the motion as if the card were showing, step it to `elapsedMs` into it, and read where
     *  the shrinking pictures are now (card pixels). */
    void setForceAnimateForTest(bool force) noexcept { forceAnimateForTest_ = force; }
    void stepSwapMotionForTest(double elapsedMs);
    std::vector<juce::Rectangle<float>> swapGhostRectsForTest() const;

    // ---- Fades (CardBodyFades.cpp) -----------------------------------------------------------------
    /** A view that opens or closes, the More row that unfolds, and a conditional section that appears or goes fade in
     *  and out with the shared FadeVisibility while the card's height follows (CardBlockFade); off screen, or with
     *  Animations Off, they land at once. */
    bool isFadeRunning() const;
    /** Test seams: the more row's and a section's fade (null without one). */
    const CardBlockFade& moreFadeForTest() const noexcept { return moreFade_; }
    const CardBlockFade& sectionFadeForTest(int section) const { return sectionFades_[(size_t)section]; }
    const CardBlockFade& viewFadeForTest(CardView view) const;

    // ---- Lookup ----------------------------------------------------------------------------------
    /** The widget bound to `paramId`, or null. */
    juce::Component* findWidget(const juce::String& paramId) const;
    /** The view `view` placed in this body, or null. */
    juce::Component* findView(CardView view) const;
    /** Opens or closes a placed view: a closed view takes no height (the card re-measures). */
    void setViewOpen(CardView view, bool open);
    bool isViewOpen(CardView view) const;
    /** The Threshold view, or null. */
    ThresholdControlComponent* getThresholdView() const;
    const CardBodyPlan& getPlan() const { return plan_; }
    /** The layout this body draws as explicit items: the resolved layout, or the automatic one
     *  written out (the same card when built from it). */
    CardLayout explicitLayout() const;
    /** The layout the app ships for this module's type, or none: where a control the card's own layout never
     *  placed belongs. */
    std::optional<CardLayout> codeDefaultLayout() const;
    /** False for the bespoke cards, which always build from the automatic plan. */
    bool drawsFromLayout() const;
    /** The node's "cardLayout" JSON this body was built from; empty for none. */
    const juce::String& builtFromOverride() const noexcept { return builtFromOverride_; }
    /** True when `node`'s override or its type's stored default changed since this body was built. */
    bool isStaleFor(const juce::AudioProcessorGraph::Node& node) const;

    // ---- Tabs (CardBodyTabs.cpp) -------------------------------------------------------------------
    /** The tab group's strip, or null; `group` indexes getPlan().tabGroups. */
    juce::Component* getTabStrip(int group) const;
    int getSelectedTab(int group) const;
    /** Shows tab `tab` of `group` and re-lays the card out (its size never changes). Out-of-range
     *  indices and the tab already shown are ignored. */
    void selectTab(int group, int tab);
    /** Runs after a tab switch has re-laid the card out (the on-card layout editor re-reads its outlines). */
    std::function<void()> onTabSelected;
    /** True for a widget in a tab section, selected or not: its CV jack stays in the card's gutter. */
    bool isTabbed(const juce::Component& widget) const;

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
    void createSegmented(CardBodyItem& item, juce::RangedAudioParameter& param);
    void createStepper(CardBodyItem& item, juce::AudioParameterInt& param);
    juce::Label* addCaption(CardBodyItem& item, juce::RangedAudioParameter& param, juce::Justification justification);
    void createSectionHeaders();
    void createTabStrips();
    void createMoreButton();
    void applyVisibility();
    bool canAnimateSwap() const;
    bool isSwapGoverned(int item) const;
    struct SwapSnapshot {
        struct Part {
            int item;
            juce::Rectangle<int> rect;
            juce::Image image;
        };
        std::vector<Part> shown;
    };
    SwapSnapshot snapshotForSwap() const;
    void pictureLeavingControls(SwapSnapshot& snapshot) const;
    void startSwapMotion(SwapSnapshot& snapshot);
    void styleFooterItems();
    void startWatchingConditions();
    void stopWatchingConditions(bool processorAlive);
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;
    int layoutItems(const std::vector<int>& indices, int columns, int y, const cardbody::BodyGeometry& g,
                    bool apply) const;
    // CardBodyFades.cpp
    void attachViewFade(int item);
    void attachMoreAndSectionFades();
    void relayoutForFade();
    void settleAfterFade();
    void refreshReveals() const;
    bool fadeOwnsMore() const;
    bool fadeOwnsSection(int section) const;
    bool isFadeableSection(int section) const;
    std::vector<juce::Component*> sectionTargets(int section) const;
    /** Starts the fades of the conditional sections `shown` flipped (and holds the ones going away); false when
     *  none could fade, which leaves them to applyVisibility. */
    bool beginSectionFades(const std::vector<bool>& wasVisible);

    ModuleComponent& card_;
    juce::AudioProcessor& module_;
    CardBodyPlan plan_;
    std::optional<CardLayout> layout_;
    juce::String builtFromOverride_;
    juce::WeakReference<ModuleCardLayoutStore> store_;
    int builtFromRevision_ = 0;
    bool moreUnfolded_ = false;
    bool dimHintsReady_ = false;
    bool forceAnimateForTest_ = false;
    std::unique_ptr<SwapMotion> swap_;
    std::vector<juce::RangedAudioParameter*> watched_; ///< Parameters this body listens to; empty once released.

    // Widgets before attachments: members unwind in reverse, so an attachment never outlives its widget.
    juce::OwnedArray<juce::Component> widgets_;
    juce::OwnedArray<juce::Component> views_;
    std::unique_ptr<CardMoreButton> moreButton_;
    // After the widgets: a fade holds a VBlank updater on one of them, so it goes first.
    std::vector<CardBlockFade> viewFades_;    ///< By item index; attached for the views.
    std::vector<CardBlockFade> sectionFades_; ///< By section index; attached for the fadeable conditional ones.
    CardBlockFade moreFade_;
    std::set<int> startingSections_; ///< Sections whose fade starts in this re-read (applyVisibility leaves them).
    bool startingMore_ = false;
    bool settlingFootprint_ = false; ///< Measuring at the final footprint (CardBlockFade::setShown).
    juce::OwnedArray<juce::SliderParameterAttachment> sliderAttachments_;
    juce::OwnedArray<juce::ComboBoxParameterAttachment> comboAttachments_;
    juce::OwnedArray<juce::ButtonParameterAttachment> buttonAttachments_;
    juce::OwnedArray<juce::ParameterAttachment> paramAttachments_;      ///< Segmented switches and steppers.
    std::shared_ptr<bool> widgetsAlive_ = std::make_shared<bool>(true); ///< False once the widgets go.

    JUCE_DECLARE_NON_COPYABLE(CardBody)
};

} // namespace synth
