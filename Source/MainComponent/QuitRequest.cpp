#include "MainComponent/QuitRequest.h"

#include "MainComponent/MainComponent.h"

namespace synth {

void requestQuit(juce::Component* mainContent, std::function<void()> quit) {
    if (auto* mc = dynamic_cast<MainComponent*>(mainContent)) {
        // Asks first, quits only on Save/Discard. quit() is the LAST thing the continuation does, and
        // juce::JUCEApplicationBase::quit() only stops the dispatch loop: shutdown(), which destroys the main window
        // and with it the MainComponent whose method invoked this continuation, runs later, after the loop unwinds.
        mc->guardUnsavedChanges("Quitting", std::move(quit));
        return;
    }
    quit();
}

} // namespace synth
