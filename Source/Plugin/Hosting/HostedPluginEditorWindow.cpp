#include "HostedPluginEditorWindow.h"

namespace synth {

namespace {
constexpr const char* kPlaceholderComponentId = "hostedPluginEditorPlaceholder";

juce::String titleFor(const HostedPluginModule& module) {
    const juce::String name = module.getPluginName();
    return name.isNotEmpty() ? name : juce::String("Hosted Plugin");
}
} // namespace

HostedPluginEditorWindow::HostedPluginEditorWindow(HostedPluginModule& module, juce::AudioProcessorGraph::NodeID nodeId)
    : juce::DocumentWindow(titleFor(module), juce::Colours::darkgrey, juce::DocumentWindow::closeButton,
                           /*addToDesktop*/ false)
    , moduleRef_(&module)
    , nodeId_(nodeId) {
    // addToDesktop=false above: constructing this window (what every headless test below does)
    // never creates a native peer. Only HostedPluginWindowManager::openEditorFor's later
    // setVisible(true) does that, for real use.
    setUsingNativeTitleBar(true);

    // The seam this window reacts to for the rest of its life — see the class comment.
    module.onInstanceChanged = [this] { instanceChanged(); };

    rebuildContent();
}

HostedPluginEditorWindow::~HostedPluginEditorWindow() {
    // Only safe to touch the module through the weak reference — see the class comment on why it
    // may already be gone (a node-delete prune can outlive the module it pointed at). Only one
    // window ever exists per module (HostedPluginWindowManager's one-per-node rule), so clearing
    // this unconditionally can never clobber some other listener's callback.
    if (auto* module = moduleRef_.get())
        module->onInstanceChanged = nullptr;

    // setContentOwned() made the content component ours; ~ResizableWindow deletes it for us.
}

void HostedPluginEditorWindow::closeButtonPressed() {
    // This window never deletes itself — the manager owns it in a map keyed by NodeID and is the
    // only thing allowed to erase (and so destroy) it. See HostedPluginWindowManager::openEditorFor.
    if (onCloseRequested)
        onCloseRequested(nodeId_);
}

void HostedPluginEditorWindow::rebuildContent() {
    auto* module = moduleRef_.get();
    auto* instance = module != nullptr ? module->getActiveInstanceForEditor() : nullptr;

    juce::AudioProcessorEditor* editor = nullptr;
    if (instance != nullptr) {
        editor = instance->createEditorIfNeeded();
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor(*instance); // hasEditor() == false
    }

    if (editor != nullptr) {
        setResizable(editor->isResizable(), false);
        setContentOwned(editor, /*resizeToFitWhenContentChangesSize*/ true);
    } else {
        // No instance right now — either a genuine unload, or the transient gap mid-swap between
        // the old instance retiring and the new one publishing (see the class comment). Neutral,
        // not an error message: HostedPluginModule::getStatusMessage() already owns error text, and
        // duplicating it here would drift.
        auto* placeholder = new juce::Label(kPlaceholderComponentId, "No plugin loaded");
        placeholder->setComponentID(kPlaceholderComponentId);
        placeholder->setJustificationType(juce::Justification::centred);
        placeholder->setSize(280, 120);
        setResizable(false, false);
        setContentOwned(placeholder, true);
    }

    if (module != nullptr)
        setName(titleFor(*module));
}

void HostedPluginEditorWindow::instanceChanged() {
    rebuildContent();

    auto* module = moduleRef_.get();
    if (module != nullptr && module->hasInstance())
        return; // rebuilt against the (possibly new) live instance — nothing else to do

    // No instance right now. A reload publishes its replacement synchronously within the SAME call
    // that retired the old one (HostedPluginModule::retireActiveInstance()'s comment), so deferring
    // this check to the next message-loop turn is what tells a real unload (still gone later) apart
    // from a swap (instance live again by the time this runs) — HostedPluginModule itself never has
    // to say which one it was.
    juce::Component::SafePointer<HostedPluginEditorWindow> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis == nullptr)
            return;
        auto* stillLive = safeThis->moduleRef_.get();
        if ((stillLive == nullptr || !stillLive->hasInstance()) && safeThis->onCloseRequested)
            safeThis->onCloseRequested(safeThis->nodeId_);
    });
}

bool HostedPluginEditorWindow::isShowingGenericEditorForTest() const {
    return dynamic_cast<juce::GenericAudioProcessorEditor*>(getContentComponent()) != nullptr;
}

bool HostedPluginEditorWindow::isShowingPlaceholderForTest() const {
    auto* content = getContentComponent();
    return content != nullptr && content->getComponentID() == kPlaceholderComponentId;
}

} // namespace synth
