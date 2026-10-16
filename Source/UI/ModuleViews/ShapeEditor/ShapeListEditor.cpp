// ShapeListEditor.cpp -- the shape editor window's content: layout, selection, the curve editor wiring and the
// buttons. The chips are in ShapeListEditorInternal.h.
#include "ShapeListEditor.h"
#include "ShapeDrawing.h"
#include "ShapeListEditorInternal.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/ModuleViews/CurveEditor/CurveWaveConversion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeFlyout.h"

namespace synth::ui {

namespace {
constexpr int kMargin = 14;
constexpr int kGap = 8;
constexpr int kMapSize = 340;
constexpr int kButtonHeight = 28;
constexpr int kBottomBarHeight = 36;
constexpr int kPollHz = 10;

void styleButton(juce::TextButton& button, const juce::String& tooltip) {
    button.setTooltip(tooltip);
    button.setTitle(button.getButtonText());
}
} // namespace

ShapeListEditor::ShapeListEditor(std::shared_ptr<ShapeListAdapter> adapter, int selected)
    : adapter_(std::move(adapter))
    , controller_(map_, *adapter_) {
    setTitle(adapter_->getWindowTitle());
    setWantsKeyboardFocus(false);

    map_.setShowLabels(true);
    addAndMakeVisible(map_);
    map_.setExplicitFocusOrder(1);

    chips_ = std::make_unique<ChipStrip>(*this);
    addAndMakeVisible(*chips_);
    chips_->setExplicitFocusOrder(2);

    curve_.setTitle("Shape curve");
    curve_.setDescription("Frequency response of the selected shape; drag points to reshape it");
    curve_.setTooltip("Shape curve: click a line to add a point, drag points, double-click a point to remove it");
    curve_.setVisibleRangeOverride(1.0);
    curve_.setZeroSegmentPx(0.0f);
    curve_.setTimeLabelFormatter([](double) { return juce::String(); });
    curve_.setAxisGuides(adapter_->getAxisGuides());
    curve_.setFillBaselineLevel(adapter_->getReferenceLevel());
    curve_.setExplicitFocusOrder(3);
    addAndMakeVisible(curve_);
    curve_.onGestureStart = [this] {
        curveGestureOpen_ = true;
        adapter_->beginEdit();
    };
    curve_.onNodeChanged = [this](int) { writeCurve(); };
    curve_.onBendChanged = [this](int) { writeCurve(); };
    curve_.onPointsChanged = [this] { writeCurve(); };
    curve_.onGestureEnd = [this] {
        writeCurve();
        adapter_->endEdit();
        curveGestureOpen_ = false;
    };

    styleButton(addButton_, "Add shape: a new shape in a free spot on the map");
    styleButton(duplicateButton_, "Duplicate: a copy of the selected shape");
    styleButton(deleteButton_, "Delete: remove the selected shape (one always stays)");
    styleButton(renameButton_, "Rename: name the selected shape");
    styleButton(startFromButton_, "Start from: replace the selected shape's curve with a vowel or an animal call");
    styleButton(drawButton_, "Draw: replace the selected shape's curve with a line, sine, triangle, saw or square");
    styleButton(doneButton_, "Done: close this window (Esc)");
    int order = 4;
    for (auto* b : {&addButton_, &duplicateButton_, &deleteButton_, &renameButton_, &startFromButton_, &drawButton_,
                    &doneButton_}) {
        b->setExplicitFocusOrder(order++);
        addAndMakeVisible(*b);
    }
    addButton_.onClick = [this] { controller_.addShapeInFreeSpot(); };
    duplicateButton_.onClick = [this] { controller_.duplicateShape(selected_); };
    deleteButton_.onClick = [this] { controller_.deleteShape(selected_); };
    renameButton_.onClick = [this] { map_.beginRename(selected_); };
    startFromButton_.onClick = [this] { openStartFromMenu(); };
    drawButton_.onClick = [this] { openDrawFlyout(); };
    doneButton_.onClick = [this] {
        if (onDone)
            onDone();
        else
            closeHostingWindow(*this);
    };

    bottomSlot_.setInterceptsMouseClicks(false, false);
    bottomSlot_.setTitle("Per-note settings");
    addAndMakeVisible(bottomSlot_);

    controller_.onSelect = [this](int index) { setSelected(index); };
    controller_.onEdit = [this](int index) { setSelected(index); };
    controller_.onChanged = [this] {
        shapes_ = adapter_->getShapes();
        lastGeneration_ = adapter_->getGeneration();
        syncAll();
    };
    map_.showMenu = [this](juce::PopupMenu& menu) { showMenuNow(menu, map_); };

    shapes_ = adapter_->getShapes();
    selected_ = juce::jlimit(0, juce::jmax(0, (int)shapes_.size() - 1), selected);
    lastGeneration_ = adapter_->getGeneration();
    setSize(kWidth, kHeight);
    syncAll();
    startTimerHz(kPollHz);
}

ShapeListEditor::~ShapeListEditor() { stopTimer(); }

juce::Component* ShapeListEditor::getChip(int index) const { return chips_->getChip(index); }

ShapeDrawRange ShapeListEditor::getDrawRange() const { return adapter_->getDrawRange(); }

/** Picks the shape the curve editor shows; the map's selection and the chips follow. */
void ShapeListEditor::setSelected(int index) {
    index = juce::jlimit(0, juce::jmax(0, (int)shapes_.size() - 1), index);
    selected_ = index;
    map_.setSelected(index);
    chips_->setSelected(index);
    loadSelectedCurve();
    updateButtonStates();
}

void ShapeListEditor::loadSelectedCurve() {
    if (curveGestureOpen_ || shapes_.empty())
        return;
    curve_.setModel(curveModelFromWave(shapes_[(size_t)selected_].curve));
}

/** Everything the list drives: the chips, the map and the curve. */
void ShapeListEditor::syncAll() {
    selected_ = juce::jlimit(0, juce::jmax(0, (int)shapes_.size() - 1), selected_);
    chips_->setShapes(shapes_);
    controller_.refresh();
    setSelected(selected_);
    resized();
}

void ShapeListEditor::refresh() {
    shapes_ = adapter_->getShapes();
    lastGeneration_ = adapter_->getGeneration();
    syncAll();
}

/** Picks up a change made elsewhere (undo, redo, the card's map, a preset load). Our own writes update
    lastGeneration_ as they go, and nothing is reloaded while a curve gesture is open. */
void ShapeListEditor::timerCallback() {
    if (curveGestureOpen_ || adapter_->getGeneration() == lastGeneration_)
        return;
    refresh();
}

void ShapeListEditor::updateButtonStates() {
    const bool several = (int)shapes_.size() > 1;
    deleteButton_.setEnabled(several);
    addButton_.setEnabled((int)shapes_.size() < adapter_->getMaxShapes());
    duplicateButton_.setEnabled((int)shapes_.size() < adapter_->getMaxShapes());
}

/** The curve editor's model goes to the module on every change, so edits are heard live. */
void ShapeListEditor::writeCurve() {
    if (shapes_.empty())
        return;
    shapes_[(size_t)selected_].curve = waveFromCurveModel(curve_.getModel());
    adapter_->setShapes(shapes_);
    lastGeneration_ = adapter_->getGeneration();
}

/** One button press as one undo step: the mutation runs between beginEdit and endEdit. */
void ShapeListEditor::edit(const std::function<void(std::vector<ShapeItem>&)>& mutate) {
    adapter_->beginEdit();
    mutate(shapes_);
    adapter_->setShapes(shapes_);
    adapter_->endEdit();
    shapes_ = adapter_->getShapes();
    lastGeneration_ = adapter_->getGeneration();
    syncAll();
}

void ShapeListEditor::applyStartFrom(int index) {
    const auto entries = adapter_->getStartFrom();
    if (index < 0 || index >= (int)entries.size() || shapes_.empty())
        return;
    const auto curve = entries[(size_t)index].curve;
    edit([&](std::vector<ShapeItem>& shapes) { shapes[(size_t)selected_].curve = curve; });
}

void ShapeListEditor::applyDrawShape(DrawShape shape) {
    if (shape == DrawShape::Free || shapes_.empty())
        return;
    const auto range = adapter_->getDrawRange();
    edit([&](std::vector<ShapeItem>& shapes) {
        shapes[(size_t)selected_].curve = curveForDrawShape(shape, shapes[(size_t)selected_].curve, range);
    });
}

void ShapeListEditor::showMenuNow(juce::PopupMenu& menu, juce::Component& target) {
    if (showMenu)
        showMenu(menu);
    else
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target));
}

void ShapeListEditor::openStartFromMenu() {
    juce::PopupMenu menu;
    juce::Component::SafePointer<ShapeListEditor> safe(this);
    const auto entries = adapter_->getStartFrom();
    for (int i = 0; i < (int)entries.size(); ++i)
        menu.addItem(entries[(size_t)i].name, [safe, i] {
            if (safe != nullptr)
                safe->applyStartFrom(i);
        });
    showMenuNow(menu, startFromButton_);
}

/** The lane Draw tool's own flyout, so the six shapes, their icons and their order are the lane's. Headless tests
    take the content through the lane's hook instead of a call-out. */
void ShapeListEditor::openDrawFlyout() {
    juce::Component::SafePointer<ShapeListEditor> safe(this);
    auto flyout = std::make_unique<DrawShapeFlyout>(
        DrawShape::Free, [](DrawShape) { return juce::String(); },
        [safe](DrawShape shape) {
            if (safe != nullptr)
                safe->applyDrawShape(shape);
        });
    if (auto& hook = test_hooks::drawShapeFlyoutHookForTest()) {
        hook(std::move(flyout));
        return;
    }
    launchDrawShapeCallOut(std::move(flyout), drawButton_.getScreenBounds(), [] { return false; });
}

bool ShapeListEditor::keyPressed(const juce::KeyPress& key) {
    if (key != juce::KeyPress::escapeKey)
        return false;
    if (onDone)
        onDone();
    else
        closeHostingWindow(*this);
    return true;
}

void ShapeListEditor::paint(juce::Graphics& g) { g.fillAll(synth::theme::themeOf(*this).colors.surface); }

void ShapeListEditor::resized() {
    auto area = getLocalBounds().reduced(kMargin);
    auto bottom = area.removeFromBottom(kBottomBarHeight);
    area.removeFromBottom(kGap);
    doneButton_.setBounds(bottom.removeFromRight(96).withSizeKeepingCentre(96, kButtonHeight));
    bottom.removeFromRight(kGap);
    bottomSlot_.setBounds(bottom);

    auto left = area.removeFromLeft(kMapSize);
    map_.setBounds(left.removeFromTop(juce::jmin(left.getHeight(), kMapSize)));
    area.removeFromLeft(kMargin);

    chips_->setBounds(area.removeFromTop(chips_->getRequiredHeight(area.getWidth())));
    area.removeFromTop(kGap);

    auto buttons = area.removeFromBottom(kButtonHeight);
    area.removeFromBottom(kGap);
    curve_.setBounds(area);

    const int widths[] = {88, 88, 72, 72, 96, 72};
    juce::TextButton* order[] = {&addButton_,    &duplicateButton_, &deleteButton_,
                                 &renameButton_, &startFromButton_, &drawButton_};
    for (int i = 0; i < 6; ++i) {
        order[i]->setBounds(buttons.removeFromLeft(widths[i]));
        buttons.removeFromLeft(kGap);
    }
}

} // namespace synth::ui
