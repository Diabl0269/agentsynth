// CurveWaveConversion.cpp -- Free-mode CurveModel <-> LfoCustomWave.
#include "CurveWaveConversion.h"

namespace synth::ui {

CurveModel curveModelFromWave(const LfoCustomWave& wave) {
    const int n = (int)wave.points.size();
    std::vector<CurveNode> nodes((size_t)n);
    for (int i = 0; i < n; ++i) {
        CurveNode node;
        node.x = (double)wave.points[(size_t)i].x;
        node.y = wave.points[(size_t)i].y;
        node.xMovable = (i != 0 && i != n - 1);
        node.yMovable = true;
        node.minY = 0.0f;
        node.maxY = 1.0f;
        nodes[(size_t)i] = node;
    }
    CurveModel model(CurveMode::Free);
    model.setNodes(std::move(nodes));
    for (int seg = 0; seg < n - 1; ++seg)
        model.setBend(seg, wave.points[(size_t)seg].bend);
    return model;
}

LfoCustomWave waveFromCurveModel(const CurveModel& model) {
    LfoCustomWave wave;
    const int n = model.getNumNodes();
    wave.points.resize((size_t)n);
    for (int i = 0; i < n; ++i) {
        const auto& node = model.getNode(i);
        wave.points[(size_t)i].x = (float)node.x;
        wave.points[(size_t)i].y = node.y;
        wave.points[(size_t)i].bend = (i < n - 1) ? model.getBend(i) : 0.0f;
    }
    wave.sanitise();
    return wave;
}

} // namespace synth::ui
