// ShapeColourSlotTests.cpp -- a shape's colour belongs to the shape, not to its place in the list: every gesture that
// adds, inserts, duplicates or deletes a shape leaves every other shape wearing the colour it wore before, on its map
// dot as in the list. One parameterised sweep over every list-changing path of the shape window, on a four-shape
// list saved without colour slots (an older project) and on one whose slots are no longer in list order.
#include "Modules/Shapes/MorphShapes.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/Library/SoundBrowser/ShapeDragPayload.h"
#include "UI/ModuleViews/MorphMap/MorphMapPalette.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include <map>
#include <set>

using namespace synth::ui;
using namespace synth::ui::test;
using synth::shapes::ShapeKind;

namespace {

bool dispatchKey(juce::Component& origin, const juce::KeyPress& press) {
    for (auto* target = &origin; target != nullptr; target = target->getParentComponent())
        if (target->keyPressed(press))
            return true;
    return false;
}

struct Rig {
    ShapeRig shapes;
    ShapeListEditor editor{shapes.adapter, 0};

    explicit Rig(bool slotted) {
        editor.setSize(ShapeListEditor::kWidth, ShapeListEditor::kPerNoteHeight);
        auto list = shapes.filter->getCustomShapes();
        const auto curve = list.shapes[0].curve;
        list.shapes.clear();
        const char* names[] = {"a", "b", "c", "d"};
        const float xs[] = {0.1f, 0.4f, 0.6f, 0.9f};
        const int slots[] = {3, 0, 5, 1}; // a list edited before: slots no longer follow the order
        for (int i = 0; i < 4; ++i) {
            synth::morph::Shape s;
            s.name = names[i];
            s.curve = curve;
            s.position = {xs[i], 0.3f};
            s.colourIndex = slotted ? slots[i] : -1;
            list.shapes.push_back(s);
        }
        shapes.filter->setCustomShapes(list);
        editor.refresh();
    }

    ShapeWindowMap& map() { return editor.getMap(); }
    juce::Component& dot(int i) { return *map().getDotStop(i); }

    /** Each shape's name and the palette slot its map dot is drawn with. */
    std::map<juce::String, int> dotSlots() {
        std::map<juce::String, int> out;
        const auto& dots = map().getDots();
        for (int i = 0; i < (int)dots.size(); ++i)
            out[dots[(size_t)i].name] = morphColourSlot(dots[(size_t)i].colourSlot, i);
        return out;
    }
    /** The same from the list itself (the chips and the key strip read it). */
    std::map<juce::String, int> listSlots() {
        std::map<juce::String, int> out;
        const auto list = shapes.adapter->getShapes();
        for (int i = 0; i < (int)list.size(); ++i)
            out[list[(size_t)i].name] = morphColourSlot(list[(size_t)i].colourIndex, i);
        return out;
    }
};

void dropOnEmptySpace(ShapeWindowMap& map, const juce::String& name) {
    const juce::DragAndDropTarget::SourceDetails details{makeShapePayload({ShapeKind::Filter, true, name}), nullptr,
                                                         map.toPixels(0.5f, 0.9f).roundToInt()};
    ASSERT_TRUE(map.isInterestedInDragSource(details));
    map.itemDragEnter(details);
    map.itemDragMove(details);
    map.itemDropped(details);
}

// The Add button's own click handler: triggerClick would post the click and run it a message later.
struct Mutation {
    const char* name;
    int sizeChange;
    std::function<void(Rig&)> run;
};

const juce::KeyPress kCtrlT('T', juce::ModifierKeys::ctrlModifier, 0);
const juce::KeyPress kCmdD('D', juce::ModifierKeys::commandModifier, 0);

const Mutation kMutations[] = {
    {"AddOnTheMap", 1, [](Rig& r) { r.editor.getController().addShapeAt(0.5f, 0.8f); }},
    {"AddButton", 1, [](Rig& r) { r.editor.getAddButton().onClick(); }},
    {"CtrlTOnAMiddleDot", 1, [](Rig& r) { dispatchKey(r.dot(1), kCtrlT); }},
    {"CtrlTOnTheLastDot", 1, [](Rig& r) { dispatchKey(r.dot(3), kCtrlT); }},
    {"CmdDOnADot", 1, [](Rig& r) { dispatchKey(r.dot(1), kCmdD); }},
    {"DuplicateMenuOrButton", 1, [](Rig& r) { r.editor.getController().duplicateShape(0); }},
    {"DeleteAMiddleDot", -1, [](Rig& r) { dispatchKey(r.dot(1), juce::KeyPress(juce::KeyPress::deleteKey)); }},
    {"DeleteTheFirstDot", -1, [](Rig& r) { r.editor.getController().deleteShape(0); }},
    {"DropFromSoundsOnEmptySpace", 1, [](Rig& r) { dropOnEmptySpace(r.map(), "eh"); }},
};

class ShapeColourSlots : public ::testing::TestWithParam<std::tuple<int, bool>> {};

} // namespace

// Regression test for FRO1336: a dot's colour came from its list index, so inserting or deleting a shape recoloured
// every shape after it
TEST_P(ShapeColourSlots, EveryOtherShapeKeepsItsColour) {
    const auto& mutation = kMutations[std::get<0>(GetParam())];
    Rig r(std::get<1>(GetParam()));
    const auto before = r.dotSlots();
    ASSERT_EQ(before, r.listSlots());
    ASSERT_EQ(before.size(), 4u);

    mutation.run(r);

    const auto after = r.dotSlots();
    ASSERT_EQ((int)after.size(), 4 + mutation.sizeChange) << mutation.name;
    EXPECT_EQ(after, r.listSlots()) << "the map and the list agree";
    std::set<int> used;
    for (const auto& [name, slot] : after) {
        EXPECT_TRUE(used.insert(slot).second) << mutation.name << ": two shapes share slot " << slot;
        if (const auto it = before.find(name); it != before.end())
            EXPECT_EQ(slot, it->second) << mutation.name << ": " << name << " changed colour";
    }

    ASSERT_TRUE(r.shapes.undo.undo()) << "one undo step";
    r.editor.refresh();
    EXPECT_EQ(r.dotSlots(), before) << mutation.name << ": undo puts every colour back";
}

INSTANTIATE_TEST_SUITE_P(EveryListChange, ShapeColourSlots,
                         ::testing::Combine(::testing::Range(0, (int)std::size(kMutations)), ::testing::Bool()),
                         [](const ::testing::TestParamInfo<std::tuple<int, bool>>& info) {
                             return std::string(kMutations[std::get<0>(info.param)].name) +
                                    (std::get<1>(info.param) ? "_Slotted" : "_Unslotted");
                         });

TEST(ShapeColourSlotsSaved, ASlotIsSavedAndReadBackAndAListWithoutSlotsKeepsNone) {
    synth::morph::Shape slotted;
    slotted.name = "a";
    slotted.colourIndex = 5;
    const auto back = synth::morph::shapesFromVar(juce::Array<juce::var>{synth::morph::shapeToVar(slotted)},
                                                  synth::LfoCustomWave::defaultWave(), false);
    ASSERT_EQ(back.size(), 1u);
    EXPECT_EQ(back[0].colourIndex, 5);

    const auto model = synth::morph::shapesFromVar(juce::Array<juce::var>{synth::morph::shapeToVar(slotted)},
                                                   synth::LfoCustomWave::defaultWave(), true);
    EXPECT_EQ(model[0].colourIndex, -1) << "a model-written patch never sets a slot";

    std::vector<synth::morph::Shape> legacy(3);
    synth::morph::sanitiseShapes(legacy);
    for (const auto& s : legacy)
        EXPECT_EQ(s.colourIndex, -1) << "an older list keeps wearing its index colours";

    std::vector<synth::morph::Shape> clash(3);
    clash[0].colourIndex = 2;
    clash[1].colourIndex = 2;
    synth::morph::sanitiseShapes(clash);
    EXPECT_EQ(clash[0].colourIndex, 2);
    EXPECT_EQ(clash[1].colourIndex, 0);
    EXPECT_EQ(clash[2].colourIndex, 1);
}
