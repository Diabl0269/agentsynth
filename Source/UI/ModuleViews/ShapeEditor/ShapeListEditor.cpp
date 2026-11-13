// ShapeListEditor.cpp -- the shape editor window's content: layout, selection, the curve editor wiring and the
// buttons. The chips are in ShapeListEditorInternal.h.
#include "ShapeListEditor.h"
#include "PitchGridRows.h"
#include "ShapeDrawing.h"
#include "ShapeListEditorInternal.h"
#include "TestMode/TestModeStateSource.h"
#include "UI/Layout/ActionTexts.h"
#include "UI/Layout/ButtonTooltip.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/ModuleViews/CurveEditor/CurveWaveConversion.h"
#include "UI/ModuleViews/FilterSpectrumAnalyser.h"
#include "UI/Settings/AccountButtonStyle.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeFlyout.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace synth::ui {

namespace {
constexpr int kMargin = 14;
constexpr int kGap = 8;
constexpr int kMapSize = 340;
constexpr int kButtonHeight = 28;
constexpr int kToolbarHeight = 26;
constexpr int kBottomBarHeight = 36;
constexpr float kMinButtonPad = ShapeDrawPenButton::kMinSidePad; // Windows fonts need only 3 in the Ring row
constexpr float kMaxButtonPad = 14.0f;
constexpr int kPollHz = 10;
constexpr int kStatusHeight = 36;
constexpr juce::uint32 kStatusShowMs = 4000;

void styleButton(juce::TextButton& button, const juce::String& explanation) {
    button.setTitle(button.getButtonText());
    setButtonTooltip(button, explanation);
}
} // namespace

/** A 1x1 child that tells test mode what the spectrum behind the curve shows. */
class ShapeListEditor::SpectrumStateProbe
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    explicit SpectrumStateProbe(const ShapeListEditor& owner)
        : owner_(owner) {
        setComponentID("shapeSpectrumState");
        setInterceptsMouseClicks(false, false);
        setSize(1, 1);
    }
    void addTestModeState(juce::DynamicObject& into) const override {
        const float peak = owner_.spectrumPeakDbForTest();
        into.setProperty("spectrumShown",
                         owner_.spectrumToggle_ != nullptr && owner_.spectrumToggle_->getToggleState());
        into.setProperty("spectrumPeakDb", (double)peak);
        into.setProperty("spectrumAtFloor", peak <= FilterSpectrumAnalyser::kMinDb + 0.01f);
    }

private:
    const ShapeListEditor& owner_;
};

ShapeListEditor::ShapeListEditor(std::shared_ptr<ShapeListAdapter> adapter, int selected)
    : adapter_(std::move(adapter))
    , controller_(map_, *adapter_) {
    setComponentID("shapeListEditor");
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
    curve_.onInsertRefused = [this](const juce::String& why) { showStatus(why); };
    curve_.onNodeChanged = [this](int) { writeCurve(); };
    curve_.onBendChanged = [this](int) { writeCurve(); };
    curve_.onPointsChanged = [this] { writeCurve(); };
    curve_.onGestureEnd = [this] {
        writeCurve();
        adapter_->endEdit();
        curveGestureOpen_ = false;
    };

    styleButton(addButton_, "A new shape in a free spot on the map");
    styleButton(duplicateButton_, "A copy of the selected shape");
    styleButton(deleteButton_, "Remove the selected shape (one always stays)");
    styleButton(renameButton_, "Name the selected shape");
    styleButton(pickShapeButton_, actiontext::kPickShapeTip); // the card's Pick shape: one action, one text
    styleButton(drawButton_, {});                             // updatePenButton gives it the tooltip of the pen state
    styleButton(saveShapeButton_, "Keep the selected shape's curve under a name, in the Sounds tab's Shapes");
    styleButton(ringButton_, "One click places dots at the harmonic ratios of a note, so a resonant "
                             "filter rings at those pitches");
    styleButton(doneButton_, actiontext::kDoneCloseWindowTip);
    int order = 3;
    buildToolbar(order);
    curve_.setExplicitFocusOrder(order++);
    std::vector<juce::TextButton*> buttons{&addButton_, &duplicateButton_, &renameButton_, &pickShapeButton_,
                                           &drawButton_};
    if (pitchRows_ != nullptr)
        buttons.push_back(&ringButton_);
    for (auto* b : {&saveShapeButton_, &deleteButton_, &doneButton_})
        buttons.push_back(b);
    for (auto* b : buttons) {
        b->setExplicitFocusOrder(order++);
        addAndMakeVisible(*b);
    }
    applyDestructiveLook(deleteButton_);
    addButton_.onClick = [this] { controller_.addShapeInFreeSpot(); };
    duplicateButton_.onClick = [this] { controller_.duplicateShape(selected_); };
    deleteButton_.onClick = [this] { controller_.deleteShape(selected_); };
    renameButton_.onClick = [this] { map_.beginRename(selected_); };
    pickShapeButton_.onClick = [this] { openPickShapeMenu(); };
    drawButton_.onClick = [this] {
        if (pen_.has_value())
            setPen(std::nullopt); // pressing it again leaves draw mode
        else
            openDrawFlyout();
    };
    curve_.onDrawRefused = [this](const juce::String& why) { showStatus(why); };
    curve_.onPenApply = [this] { return applyPenToSelection(); };
    curve_.setDrawBand(adapter_->getDrawRange().lo, adapter_->getDrawRange().hi, adapter_->getDrawRange().cycles);
    restorePen();
    ringButton_.onClick = [this] { openRingMenu(); };
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

    map_.onCursorSelected = [this](bool selected) { onCursorSelectionChanged(selected); };
    controller_.onSelect = [this](int index) { setSelected(index); };
    controller_.onEdit = [this](int index) { setSelected(index); };
    controller_.onChanged = [this] {
        shapes_ = adapter_->getShapes();
        lastGeneration_ = adapter_->getGeneration();
        syncAll();
    };
    map_.onNotice = [this](const juce::String& text) { showStatus(text); };
    map_.dropHooks.accepts = [this](synth::shapes::ShapeKind kind) { return acceptsLibraryShape(kind); };
    map_.dropHooks.dropped = [this](const synth::shapes::LibraryShape& shape, int dot, juce::Point<float> at) {
        dropLibraryShape(shape, dot, at);
    };
    statusLabel_.setJustificationType(juce::Justification::topLeft);
    statusLabel_.setInterceptsMouseClicks(false, false);
    statusLabel_.setAccessible(false); // the message is announced when it appears
    addAndMakeVisible(statusLabel_);
    map_.setEditItemShown(false); // a press already selected the dot; there is no separate edit step here
    map_.showMenu = [this](juce::PopupMenu& menu, juce::Rectangle<int> area) { showMenuNow(menu, area); };
    map_.onStripMenu = [this](MorphMap::Axis axis, juce::Rectangle<int> area) {
        if (onStripMenu)
            onStripMenu(axis, area);
    };

    shapes_ = adapter_->getShapes();
    selected_ = juce::jlimit(0, juce::jmax(0, (int)shapes_.size() - 1), selected);
    lastGeneration_ = adapter_->getGeneration();
    curveWatch_ = RestoreWatch(adapter_->restoreSerial());
    setSize(kWidth, perNote_ != nullptr ? kPerNoteHeight : kHeight);
    syncAll();
    if (spectrumToggle_ != nullptr) {
        spectrumProbe_ = std::make_unique<SpectrumStateProbe>(*this);
        addAndMakeVisible(*spectrumProbe_);
    }
    startTimerHz(kPollHz);
}

float ShapeListEditor::spectrumPeakDbForTest() const {
    return spectrumDb_.empty() ? FilterSpectrumAnalyser::kMinDb
                               : *std::max_element(spectrumDb_.begin(), spectrumDb_.end());
}

ShapeListEditor::~ShapeListEditor() { stopTimer(); }

int ShapeListEditor::toolbarHeight() const { return pitchRows_ != nullptr ? PitchGridRows::kHeight : kToolbarHeight; }

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

/** Picks up a change made elsewhere (undo, redo, the card's map, a preset load, modulation of the cursor). Our own
   writes update lastGeneration_ as they go, and nothing is reloaded while a curve gesture is open. */
void ShapeListEditor::timerCallback() {
    if (statusClearsAtMs_ != 0 && (juce::int32)(juce::Time::getMillisecondCounter() - statusClearsAtMs_) >= 0) {
        statusClearsAtMs_ = 0;
        statusLabel_.setText({}, juce::dontSendNotification);
    }
    pollResponse();
    controller_.syncCursor(); // the map's cursor follows Morph X/Y modulation
    if (pitchRows_ != nullptr)
        pitchRows_->poll();
    if (curveGestureOpen_ || adapter_->getGeneration() == lastGeneration_)
        return;
    refresh();
}

/** A refused key says why under the map, and to a screen reader, instead of doing nothing; a done note (refused
    false) reads in the muted text colour so it does not look like an error. */
void ShapeListEditor::showStatus(const juce::String& text, bool refused) {
    const auto& colors = synth::theme::themeOf(*this).colors;
    statusLabel_.setColour(juce::Label::textColourId, refused ? colors.error : colors.textMuted);
    statusLabel_.setText(text, juce::dontSendNotification);
    statusClearsAtMs_ = juce::Time::getMillisecondCounter() + kStatusShowMs;
    juce::AccessibilityHandler::postAnnouncement(text, juce::AccessibilityHandler::AnnouncementPriority::medium);
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

void ShapeListEditor::showMenuNow(juce::PopupMenu& menu, juce::Component& target) {
    if (showMenu)
        showMenu(menu);
    else
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target));
}

/** The map's dot menu: opens beside the dot. A test sees the menu through `showMenu` and the area through
    `showMenuAtArea`. */
void ShapeListEditor::showMenuNow(juce::PopupMenu& menu, juce::Rectangle<int> screenArea) {
    if (showMenuAtArea)
        showMenuAtArea(menu, screenArea);
    else if (showMenu)
        showMenu(menu);
    else
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(screenArea));
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
    statusLabel_.setBounds(left.removeFromTop(kStatusHeight).withTrimmedTop(kGap));
    area.removeFromLeft(kMargin);

    chips_->setBounds(area.removeFromTop(chips_->getRequiredHeight(area.getWidth())));
    area.removeFromTop(kGap);
    if (hasToolbar()) {
        layoutToolbar(area.removeFromTop(toolbarHeight()));
        area.removeFromTop(kGap);
    }

    auto buttons = area.removeFromBottom(kButtonHeight);
    area.removeFromBottom(kGap);
    curve_.setBounds(area);

    // Delete is the destructive action: alone at the right end of the row, apart from the others. The other buttons are
    // as wide as their words (and the Draw button's icon) plus an equal padding, the most padding that lets the row
    // fit; with Ring helpers the row holds one more button, so the padding and the gaps are smaller.
    const bool ring = pitchRows_ != nullptr;
    deleteButton_.setBounds(buttons.removeFromRight(ring ? 56 : 64));
    juce::TextButton* order[] = {&addButton_,  &duplicateButton_, &renameButton_,   &pickShapeButton_,
                                 &drawButton_, &ringButton_,      &saveShapeButton_};
    const int gap = ring ? kGap / 2 : kGap;
    std::vector<juce::TextButton*> shown;
    std::vector<float> content;
    for (auto* button : order) {
        if (!ring && button == &ringButton_)
            continue;
        const auto font = button->getLookAndFeel().getTextButtonFont(*button, kButtonHeight);
        float width = juce::GlyphArrangement::getStringWidth(font, button->getButtonText());
        if (button == &drawButton_)
            width += (float)ShapeDrawPenButton::kIconSize + ShapeDrawPenButton::kIconGap;
        shown.push_back(button);
        content.push_back(width);
    }
    float contentTotal = 0.0f;
    for (const float w : content)
        contentTotal += w;
    // One pixel per button is kept back for rounding each width up, so no word is clipped by a fraction of a pixel.
    const float room = (float)buttons.getWidth() - (float)((gap + 1) * (int)shown.size());
    const float pad = juce::jlimit(kMinButtonPad, kMaxButtonPad, (room - contentTotal) / (2.0f * (float)shown.size()));
    for (size_t i = 0; i < shown.size(); ++i) {
        shown[i]->setBounds(buttons.removeFromLeft((int)std::ceil(content[i] + 2.0f * pad)));
        buttons.removeFromLeft(gap);
    }
}

} // namespace synth::ui
