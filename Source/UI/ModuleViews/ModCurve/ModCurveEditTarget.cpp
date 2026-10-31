#include "ModCurveEditTarget.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ModuleTitle.h"
#include "Modules/AttenuverterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/KnobModSources.h"
#include "UI/Graph/ModMatrixEndpoints.h"
#include <algorithm>
#include <vector>

namespace synth::ui {

namespace {
struct ChangeWatcher {
    juce::uint32 attenuverter = 0;
    int token = 0;
    std::function<void()> hook;
};
std::vector<ChangeWatcher>& watchers() {
    static std::vector<ChangeWatcher> list;
    return list;
}

void fireChanged(juce::uint32 attenuverter) {
    // Copied first: a hook may open or close a view, which edits the list.
    std::vector<ChangeWatcher> copy;
    for (const auto& w : watchers())
        if (w.attenuverter == attenuverter)
            copy.push_back(w);
    for (const auto& w : copy) {
        const auto alive = std::any_of(watchers().begin(), watchers().end(),
                                       [&w](const ChangeWatcher& x) { return x.token == w.token; });
        if (alive)
            w.hook();
    }
}

AttenuverterModule* findAtten(GraphEditor& editor, juce::AudioProcessorGraph::NodeID id) {
    if (auto* node = editor.getAudioEngine().getGraph().getNodeForId(id))
        return dynamic_cast<AttenuverterModule*>(node->getProcessor());
    return nullptr;
}
} // namespace

ModCurveEditTarget::ModCurveEditTarget(GraphEditor& editor, juce::AudioProcessorGraph::NodeID attenuverter)
    : editor_(&editor)
    , atten_(attenuverter) {}

bool ModCurveEditTarget::exists() const { return editor_ != nullptr && findAtten(*editor_, atten_) != nullptr; }

std::optional<synth::ModCurve> ModCurveEditTarget::read() const {
    if (editor_ == nullptr)
        return std::nullopt;
    if (auto* module = findAtten(*editor_, atten_))
        return module->getCurve();
    return std::nullopt;
}

void ModCurveEditTarget::beginGesture() {
    if (exists())
        editor_->beginModAmountGesture();
}

void ModCurveEditTarget::commitGesture() {
    if (editor_ != nullptr)
        editor_->commitModAmountGesture();
}

void ModCurveEditTarget::apply(const synth::ModCurve& curve) {
    if (editor_ == nullptr)
        return;
    auto* module = findAtten(*editor_, atten_);
    if (module == nullptr || module->getCurve() == curve)
        return;
    module->setCurve(curve);
    editor_->notifyModuleContentChanged();
    fireChanged(atten_.uid);
}

void ModCurveEditTarget::setOnce(const synth::ModCurve& curve) {
    beginGesture();
    apply(curve);
    commitGesture();
}

namespace {
juce::String nodeName(GraphEditor& editor, juce::AudioProcessorGraph::NodeID id) {
    if (auto* node = editor.getAudioEngine().getGraph().getNodeForId(id))
        return synth::moduleTitle(*node);
    return {};
}
} // namespace

juce::String ModCurveEditTarget::sourceName() const {
    if (editor_ == nullptr)
        return {};
    const auto isPort = [this](juce::AudioProcessorGraph::NodeID id) {
        return editor_->getMacroController().nodeIsMacroPort(id);
    };
    const auto end = realEndpointBehindPorts(editor_->getAudioEngine().getGraph(), atten_, true, isPort);
    return end.valid() ? nodeName(*editor_, end.node) : juce::String();
}

juce::String ModCurveEditTarget::destinationName() const {
    if (editor_ == nullptr)
        return {};
    const auto isPort = [this](juce::AudioProcessorGraph::NodeID id) {
        return editor_->getMacroController().nodeIsMacroPort(id);
    };
    const auto end = realEndpointBehindPorts(editor_->getAudioEngine().getGraph(), atten_, false, isPort);
    if (!end.valid())
        return {};
    const auto param = knobModTarget(*editor_, end.node, end.channel).paramName;
    const auto card = nodeName(*editor_, end.node);
    return param.isNotEmpty() ? card + " " + param : card;
}

juce::String ModCurveEditTarget::title() const {
    const auto from = sourceName();
    const auto to = destinationName();
    return "Curve: " + (from.isNotEmpty() ? from : juce::String("source")) + " to " +
           (to.isNotEmpty() ? to : juce::String("destination"));
}

std::shared_ptr<void> ModCurveEditTarget::watchChanges(std::function<void()> hook) const {
    static int nextToken = 1;
    const int token = nextToken++;
    watchers().push_back({atten_.uid, token, std::move(hook)});
    return std::shared_ptr<void>(nullptr, [token](void*) {
        auto& list = watchers();
        list.erase(
            std::remove_if(list.begin(), list.end(), [token](const ChangeWatcher& w) { return w.token == token; }),
            list.end());
    });
}

std::shared_ptr<void> ModCurveEditTarget::watchRestore(std::function<void()> hook) const {
    if (editor_ == nullptr || editor_->getUndoManager() == nullptr)
        return nullptr;
    return editor_->getUndoManager()->addRestoreSettledHook(std::move(hook));
}

} // namespace synth::ui
