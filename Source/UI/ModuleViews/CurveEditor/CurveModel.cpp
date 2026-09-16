#include "CurveModel.h"
#include <algorithm>
#include <stdexcept>

namespace synth::ui {

CurveModel::CurveModel(CurveMode mode, ShapeFn shapeFn)
    : mode_(mode)
    , shapeFn_(std::move(shapeFn)) {}

CurveModel::ShapeFn CurveModel::defaultShape() {
    return [](float progress, float bend) { return synth::EnvelopeGenerator::shape(progress, bend); };
}

void CurveModel::setNodes(std::vector<CurveNode> nodes) {
    entries_.clear();
    entries_.reserve(nodes.size());
    for (auto& node : nodes) {
        NodeEntry entry;
        entry.node = node;
        entry.internalId = nextId_++;
        entries_.push_back(entry);
    }
}

const CurveNode& CurveModel::getNode(int index) const { return entries_.at((size_t)index).node; }

double CurveModel::getMinX() const { return entries_.empty() ? 0.0 : entries_.front().node.x; }
double CurveModel::getMaxX() const { return entries_.empty() ? 0.0 : entries_.back().node.x; }

void CurveModel::checkSegmentIndex(int segment) const {
    if (segment < 0 || segment >= getNumSegments())
        throw std::out_of_range("CurveModel: segment index out of range");
}

float CurveModel::getBend(int segment) const {
    checkSegmentIndex(segment);
    return entries_[(size_t)segment].outgoingBend;
}

void CurveModel::setBend(int segment, float bend) {
    checkSegmentIndex(segment);
    entries_[(size_t)segment].outgoingBend = juce::jlimit(-1.0f, 1.0f, bend);
}

bool CurveModel::isBendable(int segment) const {
    checkSegmentIndex(segment);
    return entries_[(size_t)segment].outgoingBendable;
}

void CurveModel::setBendable(int segment, bool bendable) {
    checkSegmentIndex(segment);
    entries_[(size_t)segment].outgoingBendable = bendable;
}

double CurveModel::segmentDuration(int segment) const {
    checkSegmentIndex(segment);
    return entries_[(size_t)segment + 1].node.x - entries_[(size_t)segment].node.x;
}

float CurveModel::valueAt(int segment, float progress) const {
    checkSegmentIndex(segment);
    const float start = entries_[(size_t)segment].node.y;
    const float end = entries_[(size_t)segment + 1].node.y;
    const float shaped = shapeFn_ ? shapeFn_(progress, entries_[(size_t)segment].outgoingBend) : progress;
    return start + (end - start) * shaped;
}

void CurveModel::setNodeY(int index, float newY) {
    auto& node = entries_.at((size_t)index).node;
    if (!node.yMovable)
        return;
    node.y = juce::jlimit(node.minY, node.maxY, newY);
}

MoveResult CurveModel::setNodeX(int index, double newX) {
    if (index < 0 || index >= getNumNodes())
        return {index};
    return mode_ == CurveMode::Fixed ? setNodeXFixed(index, newX) : setNodeXFree(index, newX);
}

MoveResult CurveModel::setNodeXFixed(int index, double newX) {
    if (index == 0 || !entries_[(size_t)index].node.xMovable)
        return {index};

    const auto& constraints = entries_[(size_t)index].node;
    const double prevX = entries_[(size_t)index - 1].node.x;
    const double requestedDuration = newX - prevX;
    const double clampedDuration = juce::jlimit(constraints.minSegment, constraints.maxSegment, requestedDuration);
    const double newNodeX = prevX + clampedDuration;
    const double delta = newNodeX - entries_[(size_t)index].node.x;

    entries_[(size_t)index].node.x = newNodeX;
    for (size_t k = (size_t)index + 1; k < entries_.size(); ++k)
        entries_[k].node.x += delta;

    return {index};
}

MoveResult CurveModel::setNodeXFree(int index, double newX) {
    if (!entries_[(size_t)index].node.xMovable)
        return {index};

    const double lo = getMinX();
    const double hi = getMaxX();
    entries_[(size_t)index].node.x = juce::jlimit(lo, hi, newX);

    const int movedId = entries_[(size_t)index].internalId;
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const NodeEntry& a, const NodeEntry& b) { return a.node.x < b.node.x; });

    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].internalId == movedId)
            return {(int)i};
    return {index}; // unreachable: the moved entry always survives the sort
}

bool CurveModel::canRemovePoint(int index) const {
    if (mode_ != CurveMode::Free)
        return false;
    if (index < 0 || index >= getNumNodes())
        return false;
    if (entries_.size() <= 2)
        return false;
    return entries_[(size_t)index].node.xMovable;
}

int CurveModel::addPoint(double x, float y) {
    auto it = std::upper_bound(entries_.begin(), entries_.end(), x,
                               [](double value, const NodeEntry& e) { return value < e.node.x; });
    const int insertAt = (int)(it - entries_.begin());
    const int splitSegment = juce::jlimit(0, juce::jmax(0, (int)entries_.size() - 2), insertAt - 1);

    NodeEntry entry;
    entry.node.x = x;
    entry.node.y = y;
    entry.internalId = nextId_++;
    if (!entries_.empty()) {
        entry.outgoingBend = entries_[(size_t)splitSegment].outgoingBend;
        entry.outgoingBendable = entries_[(size_t)splitSegment].outgoingBendable;
    }

    entries_.insert(entries_.begin() + insertAt, entry);
    return insertAt;
}

bool CurveModel::removePoint(int index) {
    if (!canRemovePoint(index))
        return false;
    entries_.erase(entries_.begin() + index);
    return true;
}

} // namespace synth::ui
