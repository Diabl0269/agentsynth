// ShapeListEditorToolbar.cpp -- the shape editor's row above the curve: the grid choice the curve snaps to and the
// Filter's Show spectrum toggle, with the settings that remember them and the gated tick that feeds the spectrum.
#include "ShapeListEditor.h"
#include "ShapeListEditorInternal.h"
#include "ShapeWindowPrefs.h"
#include "UI/Graph/CardWidgets/CardTogglePill.h"
#include "UI/ModuleViews/CurveEditor/CurveGridNoteLength.h"
#include "UI/ModuleViews/FilterSpectrumAnalyser.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kGridLabelWidth = 34;
constexpr int kSwitchWidth = 236;
constexpr int kMinBoxWidth = 96;
constexpr int kToggleWidth = 124;
constexpr float kSpectrumFillAlpha = 0.14f;
constexpr float kSpectrumLineAlpha = 0.35f;
} // namespace

bool ShapeListEditor::hasToolbar() const { return gridOffer_.has_value() || spectrumToggle_ != nullptr; }

juce::String ShapeListEditor::getGridName() const {
    if (gridBox_ != nullptr && gridBox_->getCurrentName().isNotEmpty())
        return gridBox_->getCurrentName();
    if (gridSwitch_ != nullptr && gridOffer_.has_value() && gridSwitch_->getSelectedIndex() >= 0)
        return gridOffer_->names[gridSwitch_->getSelectedIndex()];
    return "Off";
}

void ShapeListEditor::setGridName(const juce::String& name) {
    if (gridBox_ != nullptr)
        gridBox_->setCurrentName(name, juce::sendNotificationSync);
    else if (gridSwitch_ != nullptr && gridOffer_.has_value() && gridOffer_->names.contains(name))
        gridSwitch_->setSelectedIndex(gridOffer_->names.indexOf(name), juce::sendNotificationSync);
}

/** Builds what the adapter offers, in Tab order from `order`, and restores the remembered choices. */
void ShapeListEditor::buildToolbar(int& order) {
    auto* props = adapter_->getApplicationProperties();
    const auto kind = adapter_->getPreferenceKind();
    gridOffer_ = adapter_->getGridOffer();

    if (gridOffer_.has_value()) {
        const auto saved = shapewindowprefs::loadText(props, shapewindowprefs::gridKey(kind));
        const juce::String startName = gridOffer_->names.contains(saved) ? saved : gridOffer_->names[0];
        if (gridOffer_->control == ShapeGridOffer::Control::noteLengthBox) {
            gridBox_ = std::make_unique<NoteLengthBox>();
            curvegrid::setUpNoteLengthBox(*gridBox_);
            gridBox_->setApplicationProperties(props);
            gridBox_->setTitle(gridOffer_->title);
            gridBox_->setTooltip(gridOffer_->tooltip);
            gridBox_->setExplicitFocusOrder(order++);
            gridBox_->setCurrentName(startName, juce::dontSendNotification);
            gridBox_->onChange = [this] { applyGridChoice(gridBox_->getCurrentName(), true); };
            addAndMakeVisible(*gridBox_);
        } else {
            gridSwitch_ = std::make_unique<CardSegmentedSwitch>("Grid", gridOffer_->names);
            gridSwitch_->setTooltip(gridOffer_->tooltip);
            gridSwitch_->setExplicitFocusOrder(order++);
            gridSwitch_->setSelectedIndex(gridOffer_->names.indexOf(startName), juce::dontSendNotification);
            gridSwitch_->onChange = [this](int index) { applyGridChoice(gridOffer_->names[index], true); };
            addAndMakeVisible(*gridSwitch_);
        }
        applyGridChoice(startName, false);
    }

    if (adapter_->hasSpectrum()) {
        spectrumToggle_ = std::make_unique<juce::ToggleButton>("Show spectrum");
        spectrumToggle_->setTitle("Show spectrum");
        spectrumToggle_->setTooltip(
            "Show spectrum: draw the filter's live spectrum faintly behind the curve, the same reading as the "
            "card's response view");
        spectrumToggle_->setWantsKeyboardFocus(true);
        spectrumToggle_->setExplicitFocusOrder(order++);
        setTogglePillStyle(*spectrumToggle_, true);
        spectrumToggle_->onClick = [this] {
            setSpectrumShown(spectrumToggle_->getToggleState());
            shapewindowprefs::saveFlag(adapter_->getApplicationProperties(),
                                       shapewindowprefs::spectrumKey(adapter_->getPreferenceKind()),
                                       spectrumToggle_->getToggleState());
        };
        addAndMakeVisible(*spectrumToggle_);
        spectrumTicker_ = std::make_unique<SpectrumTicker>([this] { pollSpectrum(); });
        const bool shown = shapewindowprefs::loadFlag(props, shapewindowprefs::spectrumKey(kind));
        spectrumToggle_->setToggleState(shown, juce::dontSendNotification);
        setSpectrumShown(shown);
    }
}

void ShapeListEditor::layoutToolbar(juce::Rectangle<int> row) {
    gridLabelArea_ = {};
    if (spectrumToggle_ != nullptr)
        spectrumToggle_->setBounds(row.removeFromRight(kToggleWidth));
    if (gridBox_ != nullptr) {
        const int width = juce::jmax(kMinBoxWidth, synth::theme::AppLookAndFeel::comboBoxWidthToFitItems(*gridBox_));
        gridBox_->setBounds(row.removeFromLeft(width));
    } else if (gridSwitch_ != nullptr) {
        gridLabelArea_ = row.removeFromLeft(kGridLabelWidth);
        gridSwitch_->setBounds(row.removeFromLeft(kSwitchWidth));
    }
}

/** The choice goes to the curve (free when it has no grid) and, when the user made it, to the settings. */
void ShapeListEditor::applyGridChoice(const juce::String& name, bool remember) {
    if (!gridOffer_.has_value() || !gridOffer_->gridFor)
        return;
    const auto grid = gridOffer_->gridFor(name);
    curve_.setGrid(grid);
    curve_.setSnapToGrid(grid.has_value());
    if (remember)
        shapewindowprefs::saveText(adapter_->getApplicationProperties(),
                                   shapewindowprefs::gridKey(adapter_->getPreferenceKind()),
                                   grid.has_value() ? name : juce::String());
}

/** Off paints nothing extra: the curve editor has no backdrop painter and the tick is stopped. */
void ShapeListEditor::setSpectrumShown(bool shown) {
    if (shown) {
        curve_.setBackdropPainter(
            [this](juce::Graphics& g, const CurveEditorGeometry& geometry) { paintSpectrum(g, geometry); });
    } else {
        curve_.setBackdropPainter(nullptr);
        spectrumDb_.clear();
    }
    updateSpectrumTimer();
    if (shown)
        pollSpectrum();
}

void ShapeListEditor::updateSpectrumTimer() {
    if (spectrumTicker_ != nullptr)
        spectrumTicker_->setRunning(spectrumToggle_ != nullptr && spectrumToggle_->getToggleState() && isVisible());
}

bool ShapeListEditor::isSpectrumTimerRunning() const {
    return spectrumTicker_ != nullptr && spectrumTicker_->isRunning();
}

void ShapeListEditor::visibilityChanged() { updateSpectrumTimer(); }

void ShapeListEditor::parentHierarchyChanged() { updateSpectrumTimer(); }

void ShapeListEditor::pollSpectrum() {
    if (spectrumToggle_ == nullptr || !spectrumToggle_->getToggleState())
        return;
    if (const auto* db = adapter_->updateSpectrum()) {
        spectrumDb_ = *db;
        curve_.repaint();
    }
}

/** One faint filled line, the card's spectrum colour, behind the grid and the curve. Point i of the analyser is
    the i-th of n log-spaced frequencies from 20 Hz to 20 kHz, the same axis the curve's x runs on. */
void ShapeListEditor::paintSpectrum(juce::Graphics& g, const CurveEditorGeometry& geometry) const {
    const auto count = spectrumDb_.size();
    if (count < 2)
        return;
    const auto& model = curve_.getModel();
    const double minX = model.getMinX();
    const double maxX = model.getMaxX();
    const float floorY = geometry.yForLevel(0.0f);
    constexpr float range = FilterSpectrumAnalyser::kMaxDb - FilterSpectrumAnalyser::kMinDb;

    juce::Path line;
    juce::Path fill;
    for (size_t i = 0; i < count; ++i) {
        const float x = geometry.xForTime(minX + (maxX - minX) * ((double)i / (double)(count - 1)));
        const float level = juce::jlimit(0.0f, 1.0f, (spectrumDb_[i] - FilterSpectrumAnalyser::kMinDb) / range);
        const float y = geometry.yForLevel(level);
        if (i == 0) {
            line.startNewSubPath(x, y);
            fill.startNewSubPath(x, floorY);
            fill.lineTo(x, y);
        } else {
            line.lineTo(x, y);
            fill.lineTo(x, y);
        }
    }
    fill.lineTo(geometry.xForTime(maxX), floorY);
    fill.closeSubPath();

    const auto colour = synth::theme::themeOf(*this).colors.accent2;
    g.setColour(colour.withAlpha(kSpectrumFillAlpha));
    g.fillPath(fill);
    g.setColour(colour.withAlpha(kSpectrumLineAlpha));
    g.strokePath(line, juce::PathStrokeType(1.0f));
}

} // namespace synth::ui
