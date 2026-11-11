// PointEditorMatrixRows.cpp -- every row of the point editor behaviour matrix, in the order the grid prints them.
#include "PointEditorMatrixDriver.h"

namespace point_matrix {

const std::vector<MatrixRow>& matrixRows() {
    static const std::vector<MatrixRow> rows = [] {
        std::vector<MatrixRow> all;
        addCurveRows(all);
        addShapeWindowRows(all);
        addLaneRows(all);
        addEqRows(all);
        return all;
    }();
    return rows;
}

} // namespace point_matrix
