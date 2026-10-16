// ModuleShapesAdapter.cpp -- the undo bracket and the morph cursor every module's shape list adapter shares.
#include "ModuleShapesAdapter.h"

namespace synth::ui {

namespace {
constexpr const char* kCursorIds[] = {"morphX", "morphY"};

float readParameter(juce::AudioProcessor& module, const char* id) {
    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&module, id)))
        return p->get();
    return 0.5f;
}
} // namespace

ModuleShapesAdapter::ModuleShapesAdapter(ModuleBase& module, AppUndoManager* undo, juce::AudioProcessorGraph& graph,
                                         juce::AudioProcessorGraph::NodeID nodeId, juce::String moduleName)
    : module_(module)
    , moduleName_(std::move(moduleName))
    , undo_(undo)
    , graph_(graph)
    , nodeId_(nodeId) {}

/** Undo replays the module's extra state, so an edit is the state before and after; an edit that left the state
    as it was records nothing (AppUndoManager::recordNodeExtraStateChange). A begin inside an open edit is ignored. */
void ModuleShapesAdapter::beginEdit() {
    if (editOpen_)
        return;
    editOpen_ = true;
    before_ = module_.getExtraState();
}

void ModuleShapesAdapter::endEdit() {
    if (!editOpen_)
        return;
    editOpen_ = false;
    if (undo_ != nullptr)
        undo_->recordNodeExtraStateChange(graph_, nodeId_, before_, module_.getExtraState());
}

juce::Point<float> ModuleShapesAdapter::getCursor() const {
    return {readParameter(module_, kCursorIds[0]), readParameter(module_, kCursorIds[1])};
}

void ModuleShapesAdapter::setCursor(juce::Point<float> position) {
    if (auto* x = findParameterByID(&module_, kCursorIds[0]))
        x->setValueNotifyingHost(x->convertTo0to1(position.x));
    if (auto* y = findParameterByID(&module_, kCursorIds[1]))
        y->setValueNotifyingHost(y->convertTo0to1(position.y));
}

/** The cursor is two parameters, so a drag is a host gesture on both (automation records it) and an undo step
    of the graph around it, the way a knob drag on the card is. */
void ModuleShapesAdapter::beginCursorGesture() {
    if (cursorGestureOpen_)
        return;
    cursorGestureOpen_ = true;
    if (undo_ != nullptr)
        undo_->captureBeforeState(graph_);
    for (const char* id : kCursorIds)
        if (auto* p = findParameterByID(&module_, id))
            p->beginChangeGesture();
}

void ModuleShapesAdapter::endCursorGesture() {
    if (!cursorGestureOpen_)
        return;
    cursorGestureOpen_ = false;
    for (const char* id : kCursorIds)
        if (auto* p = findParameterByID(&module_, id))
            p->endChangeGesture();
    if (undo_ != nullptr)
        undo_->pushSnapshotFromCapture(graph_);
}

} // namespace synth::ui
