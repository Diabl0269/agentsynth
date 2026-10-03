// OnCardEditorOwner.cpp -- the GraphEditor-owned slot a running on-card layout editor lives in.
#include "OnCardEditorOwner.h"
#include "CardLayoutOnCardEditor.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

namespace synth::ui {

namespace {

constexpr const char* kSessionProperty = "onCardLayoutEditor";

struct Session final : juce::ReferenceCountedObject {
    std::unique_ptr<CardLayoutOnCardEditor> editor;
    JUCE_DECLARE_WEAK_REFERENCEABLE(Session)
};

} // namespace

void closeOnCardLayoutEditor(GraphEditor& graph) {
    if (auto* running = dynamic_cast<Session*>(graph.getProperties()[kSessionProperty].getObject()))
        if (running->editor != nullptr)
            running->editor->done();
}

// A closing editor cannot destroy itself from inside its own button's click, so it asks to be let go
// once the call returns; a session opened in the meantime is not touched (the weak reference is gone).
void adoptOnCardLayoutEditor(GraphEditor& graph, std::unique_ptr<CardLayoutOnCardEditor> editor) {
    auto* session = new Session();
    session->editor = std::move(editor);
    juce::WeakReference<Session> weak(session);
    juce::Component::SafePointer<GraphEditor> safeGraph(&graph);
    session->editor->onClosed = [weak, safeGraph] {
        juce::MessageManager::callAsync([weak, safeGraph] {
            if (weak.get() != nullptr && safeGraph != nullptr)
                safeGraph->getProperties().remove(kSessionProperty);
        });
    };
    graph.getProperties().set(kSessionProperty, juce::var(session));
}

} // namespace synth::ui
