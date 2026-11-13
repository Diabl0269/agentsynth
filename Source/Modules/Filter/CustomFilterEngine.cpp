// CustomFilterEngine.cpp -- shape handoff, morph blend, redesign policy and convolution for the
// Filter's Custom type, shared-cursor path. The per-voice path is CustomFilterEngineVoices.cpp.
#include "CustomFilterEngine.h"
#include "CustomFilterCutoff.h"
#include "CustomFilterPeakMorph.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace synth {

CustomFilterEngine::CustomFilterEngine() { setShapes(CustomFilterShapes::defaultShapes()); }

CustomFilterEngine::~CustomFilterEngine() = default;

std::unique_ptr<CustomFilterEngine::ShapeTable> CustomFilterEngine::renderTable(const CustomFilterShapes& shapes) {
    auto table = std::make_unique<ShapeTable>();
    table->count = (int)shapes.shapes.size();
    table->db.resize((size_t)(table->count * kGridSize));
    table->peakCount.assign((size_t)table->count, 0);
    table->peaks.assign((size_t)(table->count * ShapeTable::kMaxPeaks), 0.0f);
    for (int s = 0; s < table->count; ++s) {
        const auto& shape = shapes.shapes[(size_t)s];
        for (int i = 0; i < kGridSize; ++i) {
            const float x = (float)i / (float)(kGridSize - 1);
            // evaluate() reads [0, 1); x == 1 falls to the last point's y, which is what we want.
            table->db[(size_t)(s * kGridSize + i)] = CustomFilterShapes::yToDb(shape.curve.evaluate(x));
        }
        const float* db = table->db.data() + s * kGridSize;
        table->peakCount[(size_t)s] =
            customfilter::findPeaks(db, kGridSize, table->peaks.data() + s * ShapeTable::kMaxPeaks);
        table->x.push_back(shape.position.size() > 0 ? shape.position[0] : 0.5f);
        table->y.push_back(shape.position.size() > 1 ? shape.position[1] : 0.5f);
    }
    table->orderByX.resize((size_t)table->count);
    std::iota(table->orderByX.begin(), table->orderByX.end(), 0);
    std::stable_sort(table->orderByX.begin(), table->orderByX.end(),
                     [&](int a, int b) { return table->x[(size_t)a] < table->x[(size_t)b]; });
    return table;
}

/** Renders the list and hands it to the audio thread (LockFreeHandoff). */
void CustomFilterEngine::setShapes(const CustomFilterShapes& shapes) {
    shapes_ = shapes;
    shapes_.sanitise();
    shapesGeneration_.fetch_add(1, std::memory_order_relaxed);
    tables_.publish(renderTable(shapes_));
}

void CustomFilterEngine::prepare(double sampleRate) {
    designer_.prepare(sampleRate);
    convolver_.prepare(CustomFilterKernelDesigner::kKernelLength, kMaxChannels, 1 + kMaxVoices);
    cursors_.prepare(sampleRate);
    voiceOnOwnGroup_.fill(false);
    perVoiceActive_ = false;
    blendedDb_.assign((size_t)kGridSize, 0.0f);
    weights_.assign((size_t)CustomFilterShapes::kMaxShapes, 0.0f);
    kernel_.assign((size_t)CustomFilterKernelDesigner::kKernelLength, 0.0f);
    tableChanged_ = true; // a new sample rate moves every bin: the next block redesigns
    voiceTableDirty_.fill(true);
}

/** A mode or voice-count change also drops every voice back onto the shared kernel. */
void CustomFilterEngine::resetState() noexcept {
    convolver_.reset();
    leavePerVoice();
}

void CustomFilterEngine::blendWeights(const ShapeTable& table, float x, float y, float* out) noexcept {
    morph::blendWeights(table.x.data(), table.y.data(), table.count, x, y, out);
}

/** Cutoff is compared as the shift it causes, so one epsilon in semitones covers every frequency. */
bool CustomFilterEngine::toneMoved(const Controls& a, const Controls& b) noexcept {
    return std::abs(a.shiftSemitones - b.shiftSemitones) > kShiftEpsilon ||
           std::abs(a.depth - b.depth) > kDepthEpsilon ||
           std::abs(customfilter::cutoffShiftSemitones(a.cutoffHz) - customfilter::cutoffShiftSemitones(b.cutoffHz)) >
               kShiftEpsilon ||
           std::abs(a.resonance - b.resonance) > kResonanceEpsilon;
}

bool CustomFilterEngine::needsDesign(const Controls& c) const noexcept {
    return tableChanged_ || std::abs(c.morphX - designed_.morphX) > kMorphEpsilon ||
           std::abs(c.morphY - designed_.morphY) > kMorphEpsilon || toneMoved(c, designed_);
}

/** Blends the shapes' dB curves, their peaks moved together (CustomFilterPeakMorph; never their kernels: a sum of
    minimum-phase kernels is neither minimum phase nor the blended magnitude) and designs one kernel into kernel_.
    The per-voice path designs through here too, so both hear the same blend as blendedGridDb draws. */
void CustomFilterEngine::designKernel(const Controls& c) noexcept {
    const ShapeTable& table = *tables_.current();
    blendWeights(table, c.morphX, c.morphY, weights_.data());
    customfilter::blendShapes(table, weights_.data(), blendedDb_.data());
    designer_.design(blendedDb_.data(), c.shiftSemitones, c.depth, c.resonance, c.cutoffHz, kernel_.data());
}

/** The shared kernel (convolver group 0). */
void CustomFilterEngine::design(const Controls& c, int fadeSamples) noexcept {
    designKernel(c);
    convolver_.setKernel(0, kernel_.data(), fadeSamples);
    designed_ = c;
    tableChanged_ = false;
    designCount_.fetch_add(1, std::memory_order_relaxed);
}

/** A redesign waits while the previous kernel is still fading in (only possible with blocks
    shorter than the convolver's minimum fade); `designed_` is not updated, so the next block picks
    the change up. The fade spans this block, so a swap is block-aligned and complete by its end. */
void CustomFilterEngine::process(float* const* channels, int numChannels, int numSamples, const Controls& controls,
                                 const VoiceInput* voices, int numVoices) noexcept {
    if (!designer_.isPrepared() || numSamples <= 0)
        return;
    if (tables_.adopt()) {
        tableChanged_ = true;
        voiceTableDirty_.fill(true);
    }
    if (tables_.current() == nullptr)
        return;
    const auto settings = readPerNoteSettings();
    if (voices != nullptr && numVoices > 0 && settings.mode != CustomFilterPerNote::Mode::Off) {
        processPerVoice(channels, numChannels, numSamples, controls, voices, numVoices, settings);
        return;
    }
    if (perVoiceActive_)
        leavePerVoice();
    if (needsDesign(controls) && !convolver_.isCrossfading())
        design(controls, numSamples);
    convolver_.process(channels, numChannels, numSamples);
}

} // namespace synth
