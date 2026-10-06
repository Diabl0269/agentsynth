// CardLayoutOnCardEditor.cpp -- the on-card layout editor's session: opening over a card, keeping one
// outline per control in step with the card the source rebuilds, and ending with Done or Cancel.
// docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutOnCardEditor.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <utility>

namespace synth::ui {

namespace {

constexpr double kFadeInMs = 160.0;
constexpr double kFadeOutMs = 110.0;
constexpr double kReducedFadeMs = 80.0;
constexpr int kBarMargin = 8;
constexpr int kAddGap = 4;

// The overlay is the card and the strip under it that holds "+ Add control".
juce::Rectangle<int> overlayBoundsFor(juce::Rectangle<int> card) {
    return card.withHeight(card.getHeight() + CardLayoutOnCardEditor::kAddStripHeight);
}

} // namespace

CardLayoutOnCardEditor::CardLayoutOnCardEditor(GraphEditor& editor, ::AppUndoManager* undo,
                                               juce::AudioProcessorGraph::NodeID nodeId,
                                               const ShortcutManager* shortcuts)
    : graphEditor_(&editor)
    , nodeId_(nodeId)
    , shortcuts_(shortcuts)
    , source_(std::make_unique<BuiltInCardLayoutSource>(editor, undo, nodeId)) {
    setTitle("Layout editing");
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);
    editBar_.onCancel = [this] { cancel(); };
    editBar_.onDone = [this] { done(); };
    editBar_.onPreset = [this] {
        buildPresetMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(editBar_.getPresetButton()));
    };
    editBar_.onApplyTo = [this] {
        buildApplyToMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(editBar_.getApplyToButton()));
    };
    addAndMakeVisible(editBar_);
    addControl_.setTitle("Add control");
    addControl_.setTooltip("Add a hidden control");
    addControl_.onClick = [this] { openAddPanel(); };
    addAndMakeVisible(addControl_);
    timeTempo_.setTooltip("Show each stage once (its time or tempo control follows the card's Time/Tempo switch), "
                          "or as separate Time and Tempo groups");
    timeTempo_.onChange = [this](int index) { chooseTimeTempo(index); };
    timeTempo_.setVisible(false);
    addChildComponent(timeTempo_);
}

// Ending the session without Done or Cancel (the owner let go of it) keeps the layout, as Done does.
CardLayoutOnCardEditor::~CardLayoutOnCardEditor() {
    stopTimer();
    cancelPendingUpdate();
    closePanel();
    if (card_ != nullptr)
        card_->removeComponentListener(this);
    source_.reset();
}

ModuleComponent* CardLayoutOnCardEditor::findCard() const {
    auto* editor = graphEditor_.getComponent();
    if (editor == nullptr)
        return nullptr;
    for (auto* comp : editor->getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == nodeId_)
            return comp;
    return nullptr;
}

std::unique_ptr<CardLayoutOnCardEditor> CardLayoutOnCardEditor::open(GraphEditor& editor, ::AppUndoManager* undo,
                                                                     juce::AudioProcessorGraph::NodeID nodeId,
                                                                     const ShortcutManager* shortcuts) {
    for (auto* comp : editor.getModuleComponents()) {
        if (comp == nullptr || comp->getNodeId() != nodeId)
            continue;
        const auto* body = comp->getCardBody();
        if (body == nullptr || !body->drawsFromLayout() || comp->getParentComponent() == nullptr)
            return nullptr;
        std::unique_ptr<CardLayoutOnCardEditor> opened(new CardLayoutOnCardEditor(editor, undo, nodeId, shortcuts));
        opened->attachTo(*comp);
        opened->syncToCard();
        if (opened->isShowing()) {
            opened->setAlpha(0.0f);
            opened->fadeTo(1.0f);
            if (auto* first = opened->outlines_.getFirst())
                first->grabKeyboardFocus();
        }
        return opened;
    }
    return nullptr;
}

// The overlay is a sibling of the card, above it: a child of the card could not outlive the rebuild
// every write causes. It follows the card's bounds and rejoins whichever card the node has next.
void CardLayoutOnCardEditor::attachTo(ModuleComponent& card) {
    if (card_ != nullptr && card_ != &card)
        card_->removeComponentListener(this);
    card_ = &card;
    cardIdentity_ = &card;
    card.addComponentListener(this);
    if (auto* parent = card.getParentComponent(); parent != nullptr && getParentComponent() != parent)
        parent->addChildComponent(this);
    setVisible(true);
}

// Reads the card as it is now: its bounds, one cell per outlined control, one outline per cell.
void CardLayoutOnCardEditor::syncToCard() {
    if (closed_)
        return;
    auto* card = findCard();
    if (card == nullptr || !source_->isAlive()) {
        close(true);
        return;
    }
    if (card != card_.getComponent())
        attachTo(*card);
    drag_ = {};
    guides_.clear();
    refreshTimeTempo();
    setBounds(overlayBoundsFor(card->getBounds()));
    cells_ = collectCells(*card);
    watchTabs(*card);
    reconcileOutlines();
    toFront(false);
    repaint();
    refreshPanel();
    refreshAddPanel();
    rememberCardImage();
}

// A picture of the card as it is now, taken only when it could be animated, so a control an undo takes away
// can shrink from it after the rebuild has already dropped the control itself.
void CardLayoutOnCardEditor::rememberCardImage() {
    cardImage_ = canAnimate() && card_ != nullptr ? card_->createComponentSnapshot(card_->getLocalBounds(), true, 2.0f)
                                                  : juce::Image();
}

// The card changed under the editor (an undo or redo, a tab switch): re-sync to it without writing, then let
// the controls glide from where they were to where the restore put them. A control the card gained grows in,
// one it lost shrinks away, but only when the card was rebuilt: a tab switch just swaps what is shown.
void CardLayoutOnCardEditor::syncAfterOutsideChange() {
    const auto before = cells_;
    const auto beforeImage = cardImage_;
    const bool rebuilt = std::exchange(cardRebuilt_, false);
    syncToCard();
    if (!closed_)
        animateRestore(before, beforeImage, rebuilt);
}

void CardLayoutOnCardEditor::animateRestore(const std::vector<OnCardCell>& before, const juce::Image& beforeImage,
                                            bool rebuilt) {
    if (!canAnimate())
        return;
    const auto rectBefore = [&before](const juce::String& key) -> const OnCardCell* {
        for (const auto& c : before)
            if (c.key == key)
                return &c;
        return nullptr;
    };
    std::vector<Move> moves;
    std::vector<juce::String> arrived;
    for (const auto& cell : cells_) {
        const auto* was = rectBefore(cell.key);
        if (was == nullptr)
            arrived.push_back(cell.key);
        else if (was->rect != cell.rect)
            moves.push_back({cell.key, was->rect, cell.rect, 160.0, false});
    }
    startGlide(std::move(moves));
    if (!rebuilt)
        return;
    for (const auto& key : arrived)
        fadeInControl(key);
    for (const auto& was : before)
        if (indexOfCell(was.key) < 0)
            startShrinkGhostOf(beforeImage.getClippedImage({was.rect.getX() * 2, was.rect.getY() * 2,
                                                            was.rect.getWidth() * 2, was.rect.getHeight() * 2}),
                               was.rect);
}

// A tab switch changes which controls are on the card without resizing it: the card says so, and the
// outlines are read again.
void CardLayoutOnCardEditor::watchTabs(ModuleComponent& card) {
    if (auto* body = card.getCardBody())
        body->onTabSelected = [self = juce::Component::SafePointer<CardLayoutOnCardEditor>(this)] {
            if (self != nullptr)
                self->triggerAsyncUpdate();
        };
}

int CardLayoutOnCardEditor::indexOfCell(const juce::String& key) const {
    for (int i = 0; i < (int)cells_.size(); ++i)
        if (cells_[(size_t)i].key == key)
            return i;
    return -1;
}

// An outline that survives a write is kept, not rebuilt: the press that started a drop is still being
// delivered to it, and keyboard focus stays where it was.
void CardLayoutOnCardEditor::reconcileOutlines() {
    for (int i = outlines_.size(); --i >= 0;)
        if (indexOfCell(outlines_[i]->getParamId()) < 0)
            outlines_.remove(i);
    for (const auto& cell : cells_) {
        auto* outline = getOutlineForTest(cell.key);
        if (outline == nullptr) {
            outline = outlines_.add(new CardLayoutOutline(cell.key, cell.caption));
            wireOutline(*outline);
            addAndMakeVisible(outline);
        }
        outline->setPanelOnly(cell.panelOnly);
        outline->setCaption(cell.caption);
        outline->setCell(cell.rect);
    }
}

void CardLayoutOnCardEditor::wireOutline(CardLayoutOutline& outline) {
    const auto key = outline.getParamId();
    outline.onPress = [this, key](const juce::MouseEvent& e) { pressOn(key, e); };
    outline.onDrag = [this](const juce::MouseEvent& e) { dragTo(e); };
    outline.onRelease = [this](const juce::MouseEvent& e) { releaseOn(e); };
    outline.onKey = [this, key](const juce::KeyPress& press) { return handleKey(key, press); };
    outline.onClick = [this, key] {
        openControlPanel(key);
        if (onControlOptions)
            onControlOptions(key);
    };
}

CardLayoutOutline* CardLayoutOnCardEditor::getOutlineForTest(const juce::String& paramId) const {
    for (auto* outline : outlines_)
        if (outline->getParamId() == paramId)
            return outline;
    return nullptr;
}

void CardLayoutOnCardEditor::done() { close(true); }

void CardLayoutOnCardEditor::cancel() { close(false); }

// Done keeps the layout the writes made; Cancel puts the opening one back, as one more undo step (none when
// the layout is as it opened).
void CardLayoutOnCardEditor::close(bool keep) {
    if (closed_ || closing_)
        return;
    closing_ = true;
    closePanel();
    stopTimer();
    glidePump_.stop();
    fadePump_.stop();
    if (finishGlide_)
        std::exchange(finishGlide_, nullptr)();
    if (finishAddFade_)
        std::exchange(finishAddFade_, nullptr)();
    ghostPump_.stop();
    ghosts_.clear();
    cancelPendingUpdate();
    escapeKey_.disarm();
    drag_ = {};
    guides_.clear();
    if (keep) {
        flushNudge();
    } else {
        nudgeKey_ = {};
        source_->restoreOpeningLayout();
    }
    if (card_ != nullptr)
        card_->removeComponentListener(this);
    source_.reset();
    closed_ = true;
    setInterceptsMouseClicks(false, false);
    fadeTo(0.0f, [this] { finishClose(); });
}

void CardLayoutOnCardEditor::finishClose() {
    setVisible(false);
    if (auto* parent = getParentComponent())
        parent->removeChildComponent(this);
    if (onClosed)
        onClosed();
}

// The whole overlay (outlines, grips, bar) fades together: 160 ms out of nothing, 110 ms back into it,
// 80 ms either way under Reduce Motion; with nothing on screen it is simply there or gone.
void CardLayoutOnCardEditor::fadeTo(float target, std::function<void()> done) {
    fadePump_.stop();
    const float from = getAlpha();
    const double ms = prefersReducedMotion() ? kReducedFadeMs : (target > from ? kFadeInMs : kFadeOutMs);
    if (!isShowing()) {
        setAlpha(target);
        if (done)
            done();
        return;
    }
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto ease = target > from ? easeOutCubic : easeInCubic;
    fadePump_.run(
        ms,
        [this, from, target, start, ms, ease] {
            const auto t = (float)juce::jlimit(0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - start) / ms);
            setAlpha(from + (target - from) * ease(t));
        },
        std::move(done));
}

void CardLayoutOnCardEditor::componentMovedOrResized(juce::Component& component, bool, bool wasResized) {
    if (&component != cardIdentity_ || writing_ || closed_)
        return;
    setBounds(overlayBoundsFor(component.getBounds()));
    if (wasResized && !drag_.pressed && !finishGlide_)
        triggerAsyncUpdate();
}

// The card is rebuilt or gone for a reason that is not ours (an undo, a conditional section flipping,
// the module removed): look for the node's card again once the dust settles.
void CardLayoutOnCardEditor::componentBeingDeleted(juce::Component& component) {
    if (&component != cardIdentity_)
        return;
    cardIdentity_ = nullptr;
    if (!writing_ && !closed_) {
        cardRebuilt_ = true;
        triggerAsyncUpdate();
    }
}

void CardLayoutOnCardEditor::handleAsyncUpdate() {
    if (!closed_ && !writing_ && !drag_.moving)
        syncAfterOutsideChange();
}

void CardLayoutOnCardEditor::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    g.setColour(theme.colors.accent);
    g.drawRoundedRectangle(cardArea().toFloat().reduced(0.75f), theme.metrics.cornerRadius, 1.5f);
    g.setColour(theme.colors.accent.withAlpha(theme.metrics.guideAlpha));
    for (const auto& guide : guides_) {
        const auto p = (float)guide.position;
        const auto line = guide.vertical ? juce::Line<float>(p, (float)guide.from, p, (float)guide.to)
                                         : juce::Line<float>((float)guide.from, p, (float)guide.to, p);
        g.drawLine(line, theme.metrics.guideLineWidth);
    }
    if (!addDrag_.ghost.isEmpty()) {
        g.setColour(theme.colors.accent.withAlpha(theme.metrics.guideAlpha));
        g.fillRoundedRectangle(addDrag_.ghost.toFloat(), theme.metrics.cornerRadius);
        g.setColour(theme.colors.accent);
        g.drawRoundedRectangle(addDrag_.ghost.toFloat().reduced(0.75f), theme.metrics.cornerRadius, 1.5f);
    }
}

// The strip under the card: the Time and tempo switch (ADSR cards) on the left, "+ Add control" taking the rest.
void CardLayoutOnCardEditor::resized() {
    const int width = std::min(getWidth() - 2 * kBarMargin, CardLayoutEditBar::kMinWidth);
    const int y = (ModuleComponent::kHeaderHeight - CardLayoutEditBar::kHeight) / 2;
    editBar_.setBounds(getWidth() - width - kBarMargin, y, width, CardLayoutEditBar::kHeight);
    auto strip = getLocalBounds().removeFromBottom(kAddStripHeight).withTrimmedTop(kAddGap);
    if (timeTempo_.isVisible()) {
        timeTempo_.setBounds(strip.removeFromLeft(kTimeTempoWidth));
        strip.removeFromLeft(kBarMargin);
    }
    addControl_.setBounds(strip);
}

} // namespace synth::ui
