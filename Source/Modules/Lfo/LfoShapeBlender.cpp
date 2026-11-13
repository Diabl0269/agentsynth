// LfoShapeBlender.cpp -- shape handoff and morph blend for the LFO's Custom wave.
#include "LfoShapeBlender.h"
#include <algorithm>
#include <cmath>

namespace synth {

namespace {
constexpr float kMorphEpsilon = 1.0e-4f;
}

LfoShapeBlender::LfoShapeBlender() {
    LfoCustomWave::defaultWave().renderTable(playing_); // a valid wave before the first publish is adopted
    setShapes(LfoCustomShapes::defaultShapes());
}

std::unique_ptr<LfoShapeBlender::ShapeTables> LfoShapeBlender::renderTables(const LfoCustomShapes& shapes) {
    auto out = std::make_unique<ShapeTables>();
    out->count = (int)shapes.shapes.size();
    out->tables.resize((size_t)out->count);
    for (int s = 0; s < out->count; ++s) {
        const auto& shape = shapes.shapes[(size_t)s];
        shape.curve.renderTable(out->tables[(size_t)s]);
        out->x.push_back(shape.position.size() > 0 ? shape.position[0] : 0.5f);
        out->y.push_back(shape.position.size() > 1 ? shape.position[1] : 0.5f);
    }
    return out;
}

/** Renders the list and hands it to the audio thread (LockFreeHandoff). */
void LfoShapeBlender::setShapes(const LfoCustomShapes& shapes) {
    shapes_ = shapes;
    shapes_.sanitise();
    generation_.fetch_add(1, std::memory_order_relaxed);
    tables_.publish(renderTables(shapes_));
}

/** One shape is copied as it is (bit for bit what the wave alone renders); several are summed by the shared
    inverse-distance weights (morph::blendWeights, the Filter's own). The wrap guard entry blends like the rest, so
    it still equals entry 0. */
void LfoShapeBlender::blendInto(const ShapeTables& tables, float x, float y, float* weights,
                                LfoCustomWave::Table& out) noexcept {
    if (tables.count == 1) {
        out = tables.tables[0];
        return;
    }
    morph::blendWeights(tables.x.data(), tables.y.data(), tables.count, x, y, weights);
    out.fill(0.0f);
    for (int s = 0; s < tables.count; ++s) {
        const float w = weights[s];
        const auto& t = tables.tables[(size_t)s];
        for (size_t i = 0; i < out.size(); ++i)
            out[i] += w * t[i];
    }
}

const LfoCustomWave::Table& LfoShapeBlender::blend(float morphX, float morphY) noexcept {
    if (tables_.adopt())
        dirty_ = true;
    const auto* current = tables_.current();
    if (current == nullptr)
        return playing_;
    const bool moved = current->count > 1 &&
                       (std::abs(morphX - blendedX_) > kMorphEpsilon || std::abs(morphY - blendedY_) > kMorphEpsilon);
    if (dirty_ || moved) {
        blendInto(*current, morphX, morphY, weights_.data(), playing_);
        blendedX_ = morphX;
        blendedY_ = morphY;
        dirty_ = false;
    }
    return playing_;
}

LfoCustomWave::Table LfoShapeBlender::blendedTable(const LfoCustomShapes& shapes, float x, float y) {
    LfoCustomShapes clean = shapes;
    clean.sanitise();
    const auto tables = renderTables(clean);
    std::array<float, morph::kMaxShapes> weights{};
    LfoCustomWave::Table out{};
    blendInto(*tables, x, y, weights.data(), out);
    return out;
}

} // namespace synth
