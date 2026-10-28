// ShapeListEditor.cpp -- the shape editor window's content: layout, selection, the curve editor wiring and the
// buttons. The chips are in ShapeListEditorInternal.h.
#include "ShapeListEditor.h"
#include "ShapeDrawing.h"
#include "ShapeListEditorInternal.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/ModuleViews/CurveEditor/CurveWaveConversion.h"
#include "UI/Settings/AccountButtonStyle.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeFlyout.h"

namespace synth::ui {

namespace {
constexpr int kMargin = 14;
constexpr int kGap = 8;
constexpr int kMapSize = 340;
constexpr int kButtonHeight = 28;
constexpr int kToolbarHeight = 26;
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
    setOpaque(true); // paint() fills every pixel

    map_.setShowLabels(true);
    addAndMakeVisible(map_);
    map_.setExplicitFocusOrder(1);

    chips_ = std::make_unique<ChipStrip>(*this);
    addAndMakeVisible(*chips_);
    chips_->setExplicitFocusOrder(2);

    curve_.setTitle("Shape curve");
    curve_.setDescription("Frequency response of the selected shape; drag points to reshape it");
    curve_.setTooltip("Shape curve: click a line to add a point, drag points, double-click a point to remove it. Keys: "
                      "Left/Right pick a point, Alt+arrows move it, Up/Down change its level, Ctrl+T inserts a point "
                      "after it, Delete removes it");
    curve_.setVisibleRangeOverride(1.0);
    curve_.setZeroSegmentPx(0.0f);
    curve_.setTimeLabelFormatter([](double) { return juce::String(); });
    curve_.setAxisGuides(adapter_->getAxisGuides());
    curve_.setFillBaselineLevel(adapter_->getReferenceLevel());
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
    styleButton(startFromButton_,
                "Start from: replace the selected shape's curve with a built-in, a saved shape or a factory shape");
    styleButton(drawButton_, "Draw: replace the selected shape's curve with a line, sine, triangle, saw or square");
    styleButton(saveShapeButton_,
                "Save shape: keep the selected shape's curve under a name, in the Sounds tab's Shapes");
    styleButton(doneButton_, "Done: close this window (Esc)");
    int order = 3;
    buildToolbar(order);
    curve_.setExplicitFocusOrder(order++);
    for (auto* b : {&addButton_, &duplicateButton_, &renameButton_, &startFromButton_, &drawButton_, &saveShapeButton_,
                    &deleteButton_, &doneButton_}) {
        b->setExplicitFocusOrder(order++);
        addAndMakeVisible(*b);
    }
    applyDestructiveLook(deleteButton_);
    addButton_.onClick = [this] { controller_.addShapeInFreeSpot(); };
    duplicateButton_.onClick = [this] { controller_.duplicateShape(selected_); };
    deleteButton_.onClick = [this] { controller_.deleteShape(selected_); };
    renameButton_.onClick = [this] { map_.beginRename(selected_); };
    startFromButton_.onClick = [this] { openStartFromMenu(); };
    drawButton_.onClick = [this] { openDrawFlyout(); };
    saveShapeButton_.onClick = [this] { saveSelectedShape(); };
    doneButton_.onClick = [this] {
        if (onDone)
            onDone();
        else
            closeHostingWindow(*this);
    };

    if (adapter_->hasPerNote()) {
        perNote_ = std::make_unique<PerNoteBar>(*adapter_);
        perNote_->setExplicitFocusOrder(order - 1);
        doneButton_.setExplicitFocusOrder(order);
        perNote_->onEdited = [this] { lastGeneration_ = adapter_->getGeneration(); };
        addAndMakeVisible(*perNote_);
    }

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
    curveWatch_ = RestoreWatch(adapter_->restoreSerial());
    setSize(kWidth, perNote_ != nullptr ? kPerNoteHeight : kHeight);
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
    const bool restored = curveWatch_.take(adapter_->isRestoring(), adapter_->restoreSerial());
    curve_.setModel(curveModelFromWave(shapes_[(size_t)selected_].curve), restored);
}

/** Everything the list drives: the chips, the map and the curve. */
void ShapeListEditor::syncAll() {
    selected_ = juce::jlimit(0, juce::jmax(0, (int)shapes_.size() - 1), selected_);
    chips_->setShapes(shapes_);
    controller_.refresh();
    if (perNote_ != nullptr)
        perNote_->refresh(shapes_);
    setSelected(selected_);
    resized();
}

void ShapeListEditor::refresh() {
    shapes_ = adapter_->getShapes();
    lastGeneration_ = adapter_->getGeneration();
    syncAll();
}

/** Focus can only move to a component that is on screen, so a window still opening is retried by its owner. */
void ShapeListEditor::focusFirstControl() {
    if (map_.isShowing())
        map_.grabKeyboardFocus();
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
    const auto entries = startFromEntries();
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
        return onAppShortcut && onAppShortcut(key);
    if (onDone)
        onDone();
    else
        closeHostingWindow(*this);
    return true;
}

void ShapeListEditor::lookAndFeelChanged() {
    applyDestructiveLook(deleteButton_);
    // The window behind the content is backed with the same opaque colour, so a theme switch keeps both in step.
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->setBackgroundColour(synth::theme::windowBackgroundFor(synth::theme::themeOf(*this)));
    repaint();
}

void ShapeListEditor::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto& colors = theme.colors;
    g.fillAll(synth::theme::windowBackgroundFor(theme)); // opaque: Neon's surface is translucent
    if (!gridLabelArea_.isEmpty()) {
        g.setColour(colors.textMuted);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("Grid", gridLabelArea_, juce::Justification::centredLeft, false);
    }
}

void ShapeListEditor::resized() {
    auto area = getLocalBounds().reduced(kMargin);
    auto bottom = area.removeFromBottom(perNote_ != nullptr ? PerNoteBar::kHeight : kBottomBarHeight);
    area.removeFromBottom(kGap);
    doneButton_.setBounds(
        bottom.removeFromRight(96).removeFromTop(kBottomBarHeight).withSizeKeepingCentre(96, kButtonHeight));
    bottom.removeFromRight(kGap);
    if (perNote_ != nullptr)
        perNote_->setBounds(bottom);

    auto left = area.removeFromLeft(kMapSize);
    map_.setBounds(left.removeFromTop(juce::jmin(left.getHeight(), kMapSize)));
    area.removeFromLeft(kMargin);

    chips_->setBounds(area.removeFromTop(chips_->getRequiredHeight(area.getWidth())));
    area.removeFromTop(kGap);
    if (hasToolbar()) {
        layoutToolbar(area.removeFromTop(kToolbarHeight));
        area.removeFromTop(kGap);
    }

    auto buttons = area.removeFromBottom(kButtonHeight);
    area.removeFromBottom(kGap);
    curve_.setBounds(area);

    // Delete is the destructive action: alone at the right end of the row, apart from the others.
    deleteButton_.setBounds(buttons.removeFromRight(64));
    const int widths[] = {80, 88, 64, 88, 64, 92};
    juce::TextButton* order[] = {&addButton_,       &duplicateButton_, &renameButton_,
                                 &startFromButton_, &drawButton_,      &saveShapeButton_};
    for (int i = 0; i < 6; ++i) {
        order[i]->setBounds(buttons.removeFromLeft(widths[i]));
        buttons.removeFromLeft(kGap);
    }
}

} // namespace synth::ui
