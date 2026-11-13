#pragma once

#include "Modules/Shapes/ShapeLibrary.h"
#include "PerNoteBar.h"
#include "ShapeDrawPenButton.h"
#include "ShapeListAdapter.h"
#include "ShapeMapController.h"
#include "ShapeRingHelpers.h"
#include "ShapeWindowMap.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Layout/NoteLengthMenu/NoteLengthBox.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <vector>

namespace synth::ui {

class PitchGridRows;

/** The content of the shape editor window: a large morph map on the left; on the right a row of shape chips, the
 *  curve editor for the selected shape and the buttons that act on it (Add shape, Duplicate, Delete, Rename,
 *  Start from, Draw); the bottom bar holds the per-note settings (when the adapter has them) and Done.
 *
 *  It edits whatever the `ShapeListAdapter` holds, so it is not tied to the Filter. Edits are heard live (every
 *  change goes to the adapter at once) and each gesture or button is one undo step.
 *
 *  A row above the curve holds what the adapter offers: a grid choice the curve snaps to (note lengths for the
 *  LFO, octaves for the Filter) and, for the Filter, a "Show spectrum" toggle that draws the live spectrum behind
 *  the curve. Both choices are remembered per kind of list in the adapter's settings.
 *
 *  Keyboard: Tab visits the map and its dots and cursor, the chips, the grid choice, the spectrum toggle, the curve
 *  editor, the buttons, the per-note controls and Done, in that order. Escape closes the window (`onDone`, else the
 *  hosting window) from any of them. Ctrl+T inserts a point on the curve or a dot on the map; when it cannot, a line
 *  under the map says why for a few seconds and is announced to screen readers.
 */
class ShapeListEditor
    : public juce::Component
    , private juce::Timer {
public:
    static constexpr int kWidth = 984;
    static constexpr int kHeight = 560; // without per-note settings
    static constexpr int kPerNoteHeight = kHeight + PerNoteBar::kHeight - PerNoteBar::kRowHeight; // with them

    /** `selected` is clamped into the list. */
    ShapeListEditor(std::shared_ptr<ShapeListAdapter> adapter, int selected);
    ~ShapeListEditor() override;

    void setSelected(int index);
    int getSelected() const noexcept { return selected_; }
    int getNumShapes() const noexcept { return (int)shapes_.size(); }
    /** Re-reads the adapter (the polling timer does this when its generation moves). */
    void refresh();
    /** Gives the keyboard focus to the map, the first Tab stop. Does nothing while the editor is not on screen. */
    void focusFirstControl();

    /** Closing: Done and Escape. Unset closes the hosting window. */
    std::function<void()> onDone;
    /** Shows a menu; unset opens it as a real popup. A test replaces it to inspect the menu. */
    std::function<void(juce::PopupMenu&)> showMenu;
    /** Like `showMenu` for the map's dot menu, which also gets the screen area it opens beside; wins over `showMenu`.
     */
    std::function<void(juce::PopupMenu&, juce::Rectangle<int>)> showMenuAtArea;
    /** A right-click on the map's Morph X / Y slider: the card opens that slider's control menu at the area. Unset
     *  does nothing. */
    std::function<void(MorphMap::Axis, juce::Rectangle<int>)> onStripMenu;
    /** Offered every key the editor did not use except Escape; returns whether it took it (the window passes app
     *  shortcuts on to the main window this way). */
    std::function<bool(const juce::KeyPress&)> onAppShortcut;

    /** Stamps the curve of start-from entry `index` (the built-ins, then the library's) onto the selected shape. */
    void applyStartFrom(int index);
    /** The Start from menu's entries: the adapter's built-in curves, then the user's saved shapes of this kind, then
     *  the factory shapes that are not a built-in's curve. `savedCount` gets how many of the library's are the user's.
     */
    std::vector<ShapeStartFrom> startFromEntries(int* builtInCount = nullptr, int* savedCount = nullptr) const;
    /** Which library kind this editor's shapes belong to; none when the adapter names no kind. */
    std::optional<synth::shapes::ShapeKind> libraryKind() const;
    /** Whether a dragged library shape of `kind` can be stamped on a chip here. */
    bool acceptsLibraryShape(synth::shapes::ShapeKind kind) const { return libraryKind() == kind; }
    /** Replaces shape `index`'s curve with the library shape's, as one undo step. */
    void stampLibraryShape(int index, const synth::shapes::LibraryShape& shape);
    /** A library shape dropped on the map: on dot `dot` it stamps that shape; on empty space (-1) it adds a shape
     *  named after it at map position `at`, selected, as one undo step, or says there is no room. */
    void dropLibraryShape(const synth::shapes::LibraryShape& shape, int dot, juce::Point<float> at);
    /** Save shape: asks for a name (refusing an empty one), asks before replacing a saved shape, then writes the
     *  selected shape's curve to the library. */
    void saveSelectedShape();
    /** Applies a draw shape to the whole of the selected shape's curve (Free draws nothing here). */
    void applyDrawShape(DrawShape shape);
    /** The Draw pen: while one is chosen a drag on the curve draws that stretch (CurveDrawGesture) instead of moving
     *  points. Remembered per kind of list in the window's settings. None leaves draw mode. */
    void setPen(std::optional<DrawShape> pen);
    std::optional<DrawShape> getPen() const noexcept { return pen_; }
    /** The keyboard route: with the pen chosen, applies it to the stretch between the selected curve points (the
     *  whole curve when fewer than two are selected) as one undo step. True when it took the key. */
    bool applyPenToSelection();
    /** Runs one Ring helpers item on the selected shape, as one undo step. Needs the pitch rows (the Filter). */
    void applyRingHelper(ringhelpers::Helper helper);
    /** Opens the Stack harmonics popover (the Ring helpers menu's item does this). */
    void openStackPanel();
    /** Stack harmonics with the panel's options, as one undo step. */
    void applyStackHarmonics(const ringhelpers::StackOptions& options);

    // ---------- inspection ----------
    ShapeWindowMap& getMap() noexcept { return map_; }
    CurveEditorComponent& getCurveEditor() noexcept { return curve_; }
    ShapeMapController& getController() noexcept { return controller_; }
    juce::TextButton& getAddButton() noexcept { return addButton_; }
    juce::TextButton& getDuplicateButton() noexcept { return duplicateButton_; }
    juce::TextButton& getDeleteButton() noexcept { return deleteButton_; }
    juce::TextButton& getRenameButton() noexcept { return renameButton_; }
    juce::TextButton& getStartFromButton() noexcept { return startFromButton_; }
    juce::TextButton& getDrawButton() noexcept { return drawButton_; }
    ShapeDrawPenButton& getDrawPenButton() noexcept { return drawButton_; }
    juce::TextButton& getSaveShapeButton() noexcept { return saveShapeButton_; }
    /** The Ring helpers button; shown only with the pitch rows. */
    juce::TextButton& getRingButton() noexcept { return ringButton_; }
    juce::TextButton& getDoneButton() noexcept { return doneButton_; }
    juce::Component* getChip(int index) const;
    /** The per-note settings in the bottom bar; null when the adapter has none. */
    PerNoteBar* getPerNoteBar() noexcept { return perNote_.get(); }
    /** The grid choice the adapter offers as the note-length box or as segments; null when it offers the other (or
     *  no grid). */
    NoteLengthBox* getGridBox() noexcept { return gridBox_.get(); }
    CardSegmentedSwitch* getGridSwitch() noexcept { return gridSwitch_.get(); }
    /** The Filter's two grid rows (kind, steps, level, key, root); null for a grid offered another way. */
    PitchGridRows* getPitchRows() noexcept { return pitchRows_.get(); }
    /** The chosen grid name ("Off" when free or when there is no grid control). */
    juce::String getGridName() const;
    /** Picks a grid choice by name as a click would; an unknown name changes nothing. The frequency rows also take
     *  the old one-row names ("1/3 oct": Octaves at 1/3 with 6 dB levels) and a kind ("Notes"). */
    void setGridName(const juce::String& name);
    /** The Guides toggle (the labelled reference lines); null unless the adapter has guides. */
    juce::ToggleButton* getGuidesToggle() noexcept { return guidesToggle_.get(); }
    /** The Show spectrum toggle; null unless the adapter has a spectrum. */
    juce::ToggleButton* getSpectrumToggle() noexcept { return spectrumToggle_.get(); }
    /** Whether the spectrum timer is running: only while the toggle is on and the editor is visible. */
    bool isSpectrumTimerRunning() const;
    /** Reads the newest spectrum and repaints the curve; what the timer does each tick. */
    void pollSpectrum();
    /** Test mode: the loudest point of the spectrum drawn now, in dB (-80 at the floor), through the hidden child
     *  `shapeSpectrumState` (`spectrumPeakDb`, `spectrumAtFloor`, `spectrumShown`). */
    float spectrumPeakDbForTest() const;
    ShapeDrawRange getDrawRange() const;
    juce::String getWindowTitle() const { return adapter_->getWindowTitle(); }
    /** The message under the map ("No room after this point"); empty once it has faded. */
    juce::String getStatusText() const { return statusLabel_.getText(); }
    /** The colour the status line is drawn in: the error colour for a refusal, the muted one for a done note. */
    juce::Colour getStatusColour() const { return statusLabel_.findColour(juce::Label::textColourId); }

    void resized() override;
    void paint(juce::Graphics& g) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    class Chip;
    class ChipStrip;
    class SpectrumTicker;
    class SpectrumStateProbe;

    void timerCallback() override;
    void loadSelectedCurve();
    void writeCurve();
    void edit(const std::function<void(std::vector<ShapeItem>&)>& mutate);
    void stampCurveOnSelected(const LfoCustomWave& curve);
    void noteStartFromUsed(const juce::String& name, const LfoCustomWave& curve, bool saved);
    void finishSave(const juce::String& typed, const LfoCustomWave& curve);
    void writeShape(const juce::String& name, const LfoCustomWave& curve, bool replace);
    void openStartFromMenu();
    void openDrawFlyout();
    void restorePen();
    void updatePenButton();
    // ShapeListEditorRing.cpp
    void openRingMenu();
    void showMenuNow(juce::PopupMenu& menu, juce::Component& target);
    void showMenuNow(juce::PopupMenu& menu, juce::Rectangle<int> screenArea);
    void syncAll();
    void showStatus(const juce::String& text, bool refused = true);
    void updateButtonStates();
    // ShapeListEditorToolbar.cpp
    bool hasToolbar() const;
    void buildToolbar(int& focusOrder);
    void layoutToolbar(juce::Rectangle<int> row);
    void applyGridChoice(const juce::String& name, bool remember);
    void applyCurveGrid(const std::optional<CurveEditorComponent::CurveGrid>& grid);
    int toolbarHeight() const;
    void setSpectrumShown(bool shown);
    void updateSpectrumTimer();
    void paintSpectrum(juce::Graphics& g, const CurveEditorGeometry& geometry) const;
    void paintResponse(juce::Graphics& g, const CurveEditorGeometry& geometry) const;
    void updateBackdrop();
    void pollResponse();
    void onCursorSelectionChanged(bool selected);

    std::shared_ptr<ShapeListAdapter> adapter_;
    std::vector<ShapeItem> shapes_;
    int selected_ = 0;
    int lastGeneration_ = -1;
    RestoreWatch curveWatch_;
    bool curveGestureOpen_ = false;
    std::optional<DrawShape> pen_;
    DrawShape lastPen_ = DrawShape::Free;

    ShapeWindowMap map_;
    ShapeMapController controller_;
    std::unique_ptr<ChipStrip> chips_;
    CurveEditorComponent curve_;
    juce::TextButton addButton_{"Add shape"};
    juce::TextButton duplicateButton_{"Duplicate"};
    juce::TextButton deleteButton_{"Delete"};
    juce::TextButton renameButton_{"Rename"};
    juce::TextButton startFromButton_{"Start from"};
    ShapeDrawPenButton drawButton_{"Draw"};
    juce::TextButton saveShapeButton_{"Save shape"};
    juce::TextButton ringButton_{"Ring helpers"};
    ringhelpers::StackOptions stackOptions_; ///< the Stack harmonics panel's last choices, for this window's life
    juce::TextButton doneButton_{"Done"};
    juce::Label statusLabel_;
    juce::uint32 statusClearsAtMs_ = 0;
    std::unique_ptr<PerNoteBar> perNote_;
    std::optional<ShapeGridOffer> gridOffer_;
    std::unique_ptr<NoteLengthBox> gridBox_;
    std::unique_ptr<CardSegmentedSwitch> gridSwitch_;
    std::unique_ptr<PitchGridRows> pitchRows_;
    std::unique_ptr<juce::ToggleButton> guidesToggle_;
    std::unique_ptr<juce::ToggleButton> spectrumToggle_;
    std::unique_ptr<SpectrumTicker> spectrumTicker_;
    std::unique_ptr<SpectrumStateProbe> spectrumProbe_;
    std::vector<float> spectrumDb_;
    std::vector<float> responseDb_; ///< the live response, kept only while the cursor is the selection
    juce::Rectangle<int> gridLabelArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShapeListEditor)
};

} // namespace synth::ui
