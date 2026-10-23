// ShapeListEditorToolbar.cpp -- the shape editor's row above the curve: the grid choice the curve snaps to, with the
// setting that remembers it.
#include "ShapeListEditor.h"
#include "ShapeListEditorInternal.h"
#include "ShapeWindowPrefs.h"
#include "UI/ModuleViews/CurveEditor/CurveGridNoteLength.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kGridLabelWidth = 34;
constexpr int kSwitchWidth = 236;
constexpr int kMinBoxWidth = 96;
} // namespace

bool ShapeListEditor::hasToolbar() const { return gridOffer_.has_value(); }

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
}

void ShapeListEditor::layoutToolbar(juce::Rectangle<int> row) {
    gridLabelArea_ = {};
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

} // namespace synth::ui
