// PointEditorMatrixProbes.h -- the behaviour columns of the point editor matrix. A probe drives a fresh editor
// through its real handlers and returns "" when the behaviour is there, else one line saying what it saw instead.
#pragma once

#include "PointEditorMatrixDriver.h"

namespace point_matrix {

struct Behaviour {
    const char* name;
    std::string (*probe)(PointDriver&);
};

/** The columns, in the order the grid prints them. */
const std::vector<Behaviour>& behaviours();

} // namespace point_matrix
