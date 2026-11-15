// UndoGlideSurfacesColour.cpp -- the registry fixtures for a colour changed through its real commit path: the morph
// map's dot colour (the card's map and the shape window's) and the shape window's chips. drawn() is the red channel
// (0..255) of the colour as drawn; the two colours differ in red by construction (docs/layout/undo-glide.md).
#include "../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeMapController.h"
#include "UndoGlideSurfaces.h"

namespace undo_glide_test {

namespace {

using synth::ui::MorphMap;
using synth::ui::ShapeListEditor;
using synth::ui::ShapeMapController;

enum class Where { cardMap, windowMap, windowChip };

/** Shape 0's colour, changed the way the colour picker's close does (one undo step). */
class DotColourSurface : public Surface {
public:
    explicit DotColourSurface(Where where)
        : where_(where) {
        MorphMap::setAnimateOffScreenForTest(true);
        if (where_ == Where::cardMap) {
            map_ = std::make_unique<MorphMap>();
            map_->setSize(252, 150);
            controller_ = std::make_unique<ShapeMapController>(*map_, *rig_.adapter);
            controller_->refresh();
        } else {
            editor_ = std::make_unique<ShapeListEditor>(rig_.adapter, 0);
        }
    }
    ~DotColourSurface() override { MorphMap::setAnimateOffScreenForTest(false); }

    void move() override {
        const auto was = drawnColour();
        // A colour whose red is far from the old one.
        const auto to = juce::Colour::fromRGB(was.getRed() < 128 ? 255 : 0, 90, 200);
        mapOwner().onCommitDotColour(0, std::nullopt, std::optional<juce::uint32>(to.getARGB()));
        refreshFromModel();
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override { return drawnColour().getRed(); }
    bool gliding() const override {
        return where_ == Where::windowChip ? editor_->isChipColourGliding(0) : mapOwner().isMoveGliding();
    }
    void stepTo(double elapsedMs) override {
        if (where_ == Where::windowChip)
            editor_->stepChipColourGlideForTest(elapsedMs);
        else
            mapOwner().stepMoveGlideForTest(elapsedMs);
    }
    juce::Component* paintedComponent() override {
        return where_ == Where::windowChip ? editor_->getChip(0) : &mapOwner();
    }

private:
    MorphMap& mapOwner() const { return where_ == Where::cardMap ? *map_ : editor_->getMap(); }
    juce::Colour drawnColour() const {
        return where_ == Where::windowChip ? editor_->getDrawnChipColour(0) : mapOwner().getDrawnDotColour(0);
    }
    void refreshFromModel() {
        if (where_ == Where::cardMap)
            controller_->refresh();
        else
            editor_->refresh();
    }
    bool restore(bool forward) {
        const bool did = forward ? rig_.undo.redo() : rig_.undo.undo();
        refreshFromModel(); // what the card's poll and the shape window's poll run after a restore
        return did;
    }

    Where where_;
    synth::ui::test::ShapeRig rig_;
    std::unique_ptr<MorphMap> map_;
    std::unique_ptr<ShapeMapController> controller_;
    std::unique_ptr<ShapeListEditor> editor_;
};

} // namespace

void appendColourEntries(std::vector<Entry>& list) {
    list.push_back({"Morph map dot colour (module card)", false,
                    [] { return std::make_unique<DotColourSurface>(Where::cardMap); }});
    list.push_back({"Morph map dot colour (shape window)", false,
                    [] { return std::make_unique<DotColourSurface>(Where::windowMap); }});
    list.push_back(
        {"Shape window chip colour", false, [] { return std::make_unique<DotColourSurface>(Where::windowChip); }});
    appendTrackColourEntries(list);
}

const std::vector<const char*>& colourSurfacesNotYetCovered() {
    // Shrink-only: never add. Each is a colour an undo or redo still snaps (file of its commit path).
    static const std::vector<const char*> gaps{
        "Track colour via the channel link (Source/UI/Timeline/TrackChannelLinkController.cpp)",
        "Module card colour override (Source/UI/Graph/MacroGroupController/MacroGroupControllerGrouping.cpp, "
        "docs/layout/colour-overrides.md)",
        "Macro card and border colour (Source/UI/Graph/GraphEditor/GraphEditorMacroPrompts.cpp)",
        "Macro port colour (Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogInternal.h)",
        "Section colour (Source/UI/Timeline/TimelineSectionStrip/TimelineSectionStripMenu.cpp)",
        "Ruler marker colour (Source/UI/Timeline/TimelineRulerComponent.cpp)",
        "Per-note shape strip colour (Source/UI/ModuleViews/ShapeEditor/NoteKeyStrip.cpp)",
    };
    return gaps;
}

} // namespace undo_glide_test
