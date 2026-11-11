// PointEditorBehaviourMatrixTests.cpp -- every point editor edits points the same way (docs/control/point-editing.md),
// checked by one matrix: the rows are the editors (a driver each, PointEditorMatrix*Rows.cpp), the columns the shared
// behaviours (PointEditorMatrixProbes.cpp), and every cell runs the same script of real key and mouse events through
// the editor's own handlers. A cell is one of:
//   pass            the behaviour works (the default);
//   NotApplicable   the editor cannot have it, with the reason (kNotApplicable);
//   missing         it does not work yet (kMissingBehaviours: editor, behaviour, why, ticket).
// kMissingBehaviours may only shrink: a missing cell that now passes fails the test until it is removed, and a cell
// that is neither listed nor passing fails by name. PointEditorDiscoveryTests.cpp checks that every point editor in
// Source is a row (or is named not to be one).
#include "PointEditorMatrixProbes.h"
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

using namespace point_matrix;

namespace {

struct Cell {
    const char* editor;
    const char* behaviour;
    const char* reason;
};

struct MissingCell {
    const char* editor;
    const char* behaviour;
    const char* reason;
    const char* ticket;
};

constexpr const char* kFixedView = "the whole curve is always drawn, so a drag cannot leave the view";
constexpr const char* kFixedShape = "an envelope curve has a fixed set of points: none is added, copied or removed";

const std::vector<Cell> kNotApplicable = {
    {"Curve editor (free)", "edge-scroll", kFixedView},
    {"Curve editor (shape window)", "edge-scroll", kFixedView},
    {"Mod curve editor", "edge-scroll", kFixedView},
    {"Curve editor (fixed)", "edge-scroll", kFixedView},
    {"Curve editor (fixed)", "duplicate", kFixedShape},
    {"Curve editor (fixed)", "delete-next", kFixedShape},
    {"Curve editor (fixed)", "double-click-add", kFixedShape},
    {"Curve editor (fixed)", "right-click-menu", "a fixed point has no menu: nothing can be deleted"},
    {"Morph map", "box-select", "a 2D map keeps one selected dot"},
    {"Morph map", "shift-extend", "a 2D map has no order to extend along"},
    {"Morph map", "edge-scroll", "the map is drawn whole and a dot clamps inside it"},
    {"EQ curve", "edge-scroll", "the frequency window is fixed"},
    {"EQ curve", "duplicate", "four fixed band slots: a band has no copy"},
};

// Behaviours an editor has not got yet. May only shrink.
const std::vector<MissingCell> kMissingBehaviours = {
    {"Mod curve editor", "double-click-add",
     "a double-click on empty space does not leave a new point in the module's curve", "TICKET"},
    {"Mod curve editor", "right-click-menu", "a right-click on a point removes it at once; there is no menu", "TICKET"},
    {"EQ curve", "box-select", "a drag on empty space does not select bands in a box; the selection is one band",
     "TICKET"},
    {"EQ curve", "shift-extend", "Shift+Left/Right moves the one selected band instead of extending", "TICKET"},
    {"EQ curve", "held-glide", "an arrow steps once per press; holding it does not glide", "TICKET"},
    {"EQ curve", "two-arrows-diagonal", "no key hold, so two arrows cannot move a band diagonally", "TICKET"},
    {"EQ curve", "delete-next",
     "Delete switches the band off and the selection stays on it, so Return can bring it back", "TICKET"},
    {"EQ curve", "right-click-menu", "a right-click on a band opens no menu", "TICKET"},
};

const Cell* findNotApplicable(const std::string& editor, const std::string& behaviour) {
    for (const auto& cell : kNotApplicable)
        if (editor == cell.editor && behaviour == cell.behaviour)
            return &cell;
    return nullptr;
}

const MissingCell* findMissing(const std::string& editor, const std::string& behaviour) {
    for (const auto& cell : kMissingBehaviours)
        if (editor == cell.editor && behaviour == cell.behaviour)
            return &cell;
    return nullptr;
}

} // namespace

TEST(PointEditorBehaviourMatrix, EveryEditorHasEveryBehaviourOrSaysWhyNot) {
    const auto& rows = matrixRows();
    const auto& columns = behaviours();
    ASSERT_GE(rows.size(), 7u);

    std::set<std::string> editors, names;
    for (const auto& row : rows)
        editors.insert(row.name);
    for (const auto& column : columns)
        names.insert(column.name);
    for (const auto& cell : kNotApplicable) {
        EXPECT_TRUE(editors.count(cell.editor) && names.count(cell.behaviour))
            << "stale: kNotApplicable names " << cell.editor << " / " << cell.behaviour;
        EXPECT_GT(std::string(cell.reason).size(), 12u) << cell.editor << ": a NotApplicable cell needs its reason";
        EXPECT_EQ(findMissing(cell.editor, cell.behaviour), nullptr)
            << cell.editor << " / " << cell.behaviour << " is both NotApplicable and missing";
    }
    for (const auto& cell : kMissingBehaviours) {
        EXPECT_TRUE(editors.count(cell.editor) && names.count(cell.behaviour))
            << "stale: kMissingBehaviours names " << cell.editor << " / " << cell.behaviour;
        EXPECT_GT(std::string(cell.reason).size(), 12u) << cell.editor << ": a missing cell needs its reason";
        EXPECT_GT(std::string(cell.ticket).size(), 0u) << cell.editor << ": a missing cell needs its ticket";
    }

    std::ostringstream grid;
    grid << "\npoint editor matrix (ok = passes, n/a = not applicable, MISSING = listed in kMissingBehaviours)\n";
    for (const auto& row : rows) {
        grid << "  " << row.name << ":";
        for (const auto& column : columns) {
            const std::string where = row.name + " / " + column.name;
            if (findNotApplicable(row.name, column.name)) {
                grid << " " << column.name << "=n/a";
                continue;
            }
            const auto driver = row.make();
            const std::string problem = column.probe(*driver);
            if (const auto* missing = findMissing(row.name, column.name)) {
                grid << " " << column.name << "=MISSING";
                if (problem.empty())
                    ADD_FAILURE() << where << " now works: remove it from kMissingBehaviours (" << missing->ticket
                                  << ")";
            } else if (problem.empty()) {
                grid << " " << column.name << "=ok";
            } else {
                grid << " " << column.name << "=FAIL";
                ADD_FAILURE() << where << ": " << problem
                              << " (fix the editor, or list the cell in kMissingBehaviours with a ticket)";
            }
        }
        grid << "\n";
    }
    std::cout << grid.str();
}
