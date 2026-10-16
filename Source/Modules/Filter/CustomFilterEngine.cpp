// CustomFilterEngine.cpp -- shape handoff, morph blend, redesign policy and convolution for the
// Filter's Custom type, shared-cursor path. The per-voice path is CustomFilterEngineVoices.cpp.
#include "CustomFilterEngine.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace synth {

namespace {
constexpr float kMinDistance = 0.02f;
constexpr float kDistancePower = 2.5f;
} // namespace

CustomFilterEngine::CustomFilterEngine() { setShapes(CustomFilterShapes::defaultShapes()); }

/** Runs once the audio thread can no longer call process() (the module is being destroyed), so
    every table -- pending, current and retired -- is freed here. */
CustomFilterEngine::~CustomFilterEngine() {
    delete pending_.exchange(nullptr);
    delete current_;
    freeRetiredTables();
}

std::unique_ptr<CustomFilterEngine::ShapeTable> CustomFilterEngine::renderTable(const CustomFilterShapes& shapes) {
    auto table = std::make_unique<ShapeTable>();
    table->count = (int)shapes.shapes.size();
    table->db.resize((size_t)(table->count * kGridSize));
    for (int s = 0; s < table->count; ++s) {
        const auto& shape = shapes.shapes[(size_t)s];
        for (int i = 0; i < kGridSize; ++i) {
            const float x = (float)i / (float)(kGridSize - 1);
            // evaluate() reads [0, 1); x == 1 falls to the last point's y, which is what we want.
            table->db[(size_t)(s * kGridSize + i)] = CustomFilterShapes::yToDb(shape.curve.evaluate(x));
        }
        table->x.push_back(shape.position.size() > 0 ? shape.position[0] : 0.5f);
        table->y.push_back(shape.position.size() > 1 ? shape.position[1] : 0.5f);
    }
    table->orderByX.resize((size_t)table->count);
    std::iota(table->orderByX.begin(), table->orderByX.end(), 0);
    std::stable_sort(table->orderByX.begin(), table->orderByX.end(),
                     [&](int a, int b) { return table->x[(size_t)a] < table->x[(size_t)b]; });
    return table;
}

/** The handoff is a single pending slot plus a retire FIFO, so the audio thread never allocates or
    frees a table:
    - The message thread renders a table and EXCHANGES it into `pending_`. If it gets a table back,
      the audio thread never took that one, so it is freed right here, safely: the exchange is the
      only way the audio thread could also have claimed it.
    - The audio thread exchanges `pending_` with null at the top of a block; on a hit it pushes its
      old `current_` into the retire FIFO and adopts the new one.
    - The message thread frees whatever is in the retire FIFO on every publish (and the destructor
      on teardown). The FIFO only fills if the audio thread adopts more than kRetireCapacity tables
      between two publishes, which cannot happen (one adoption per publish); if it ever did, the old
      table would be leaked rather than freed on the audio thread. */
void CustomFilterEngine::setShapes(const CustomFilterShapes& shapes) {
    shapes_ = shapes;
    shapes_.sanitise();
    shapesGeneration_.fetch_add(1, std::memory_order_relaxed);
    auto table = renderTable(shapes_);
    freeRetiredTables();
    delete pending_.exchange(table.release(), std::memory_order_acq_rel);
}

void CustomFilterEngine::freeRetiredTables() noexcept {
    const auto scope = retireFifo_.read(retireFifo_.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i)
        delete retired_[(size_t)(scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i)
        delete retired_[(size_t)(scope.startIndex2 + i)];
}

void CustomFilterEngine::adoptPendingTable() noexcept {
    ShapeTable* incoming = pending_.exchange(nullptr, std::memory_order_acq_rel);
    if (incoming == nullptr)
        return;
    if (current_ != nullptr) {
        const auto scope = retireFifo_.write(1);
        if (scope.blockSize1 > 0)
            retired_[(size_t)scope.startIndex1] = current_;
    }
    current_ = incoming;
    tableChanged_ = true;
    voiceTableDirty_.fill(true);
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

/** w_i = 1 / max(d_i, kMinDistance)^kDistancePower, normalised. The distance floor keeps a cursor
    sitting on a shape finite while still letting that shape dominate (~1e4 : 1 against a shape a
    whole map-width away). */
void CustomFilterEngine::blendWeights(const ShapeTable& table, float x, float y, float* out) noexcept {
    float sum = 0.0f;
    for (int s = 0; s < table.count; ++s) {
        const float dx = x - table.x[(size_t)s];
        const float dy = y - table.y[(size_t)s];
        const float d = juce::jmax(kMinDistance, std::sqrt(dx * dx + dy * dy));
        out[s] = 1.0f / std::pow(d, kDistancePower);
        sum += out[s];
    }
    for (int s = 0; s < table.count; ++s)
        out[s] /= sum;
}

bool CustomFilterEngine::needsDesign(const Controls& c) const noexcept {
    return tableChanged_ || std::abs(c.morphX - designed_.morphX) > kMorphEpsilon ||
           std::abs(c.morphY - designed_.morphY) > kMorphEpsilon ||
           std::abs(c.shiftSemitones - designed_.shiftSemitones) > kShiftEpsilon ||
           std::abs(c.depth - designed_.depth) > kDepthEpsilon;
}

/** Blends the shapes' dB curves (never their kernels: a sum of minimum-phase kernels is neither
    minimum phase nor the blended magnitude) and designs one kernel into kernel_. */
void CustomFilterEngine::designKernel(const Controls& c) noexcept {
    const ShapeTable& table = *current_;
    blendWeights(table, c.morphX, c.morphY, weights_.data());
    std::fill(blendedDb_.begin(), blendedDb_.end(), 0.0f);
    for (int s = 0; s < table.count; ++s)
        juce::FloatVectorOperations::addWithMultiply(blendedDb_.data(), table.db.data() + s * kGridSize,
                                                     weights_[(size_t)s], kGridSize);
    designer_.design(blendedDb_.data(), c.shiftSemitones, c.depth, kernel_.data());
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
    adoptPendingTable();
    if (current_ == nullptr)
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
