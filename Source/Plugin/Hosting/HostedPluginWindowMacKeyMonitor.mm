// Concern: FRO337 -- the mac-only half of "Esc/Cmd+W closes a focused hosted-plugin window".
#include "HostedPluginWindowMacKeyMonitor.h"

#if JUCE_MAC

#include "HostedPluginEditorWindow.h"
#import <AppKit/AppKit.h>

namespace synth {

// This monitor handles Cmd+W ONLY. Esc is deliberately NOT intercepted here: verified in-app
// against Apple's own AUDelay (a native NSView AU editor), an Esc the plugin's own view doesn't
// consume travels up the AppKit responder chain to JUCE's peer view and reaches
// HostedPluginEditorWindow::keyPressed from there, exactly like it would for a plugin whose editor
// is a real juce::Component -- there is no separate Esc seam to maintain here. Cmd+W is different:
// it is a key equivalent the app's own menus would otherwise claim before AppKit's normal
// responder-chain delivery ever reaches the plugin's view, so a local NSEvent monitor -- which
// sees every keyDown delivered to THIS PROCESS before AppKit routes it anywhere, menu key
// equivalents included -- is the only seam that can intercept it ahead of that. Scoped to this
// window alone (checked against [NSApp keyWindow] on every event) so it never swallows a keystroke
// meant for some other window, including another plugin's.
class HostedPluginWindowMacKeyMonitor::Impl {
public:
    explicit Impl(HostedPluginEditorWindow& windowIn)
        : window(windowIn) {
        // Never install a real, process-wide OS hook for a window with no native peer yet --
        // every headless test (HostedPluginEditorWindowTests.cpp) constructs windows this way, and
        // HostedPluginWindowManagerNativeWindowTest's RecordingHostedPluginWindowManager overrides
        // addWindowToDesktop() to never create one even with setCreatesNativeWindows(true), so the
        // production call site right after it (HostedPluginWindowManager::openEditorFor) can't
        // itself guarantee a peer exists here.
        if (window.getPeer() == nullptr)
            return;
        // __block-free: the monitor block captures `this` by value (a raw pointer is fine -- the
        // monitor is removed in ~Impl(), before `this` is ever freed) and re-resolves the window's
        // NSWindow* on every event rather than caching it once, since a peer can in principle be
        // recreated (lookAndFeelChanged()/recreateDesktopWindow()) after installation.
        monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                        handler:^NSEvent*(NSEvent* event) {
                                                            return this->handleKeyDown(event);
                                                        }];
    }

    ~Impl() {
        if (monitor != nil)
            [NSEvent removeMonitor:monitor];
    }

private:
    // Returns `event` unchanged to let it through, or nil to swallow it. Never touches `window`
    // beyond reading its peer/NodeID here -- the actual close is deferred (see below), so this
    // never destroys `window` from inside AppKit's own event-dispatch call stack.
    NSEvent* handleKeyDown(NSEvent* event) {
        auto* peer = window.getPeer();
        if (peer == nullptr)
            return event; // defensive only -- the ctor above already refuses to install without one

        auto* ourWindow = [(__bridge NSView*)peer->getNativeHandle() window];
        if (ourWindow == nil || [NSApp keyWindow] != ourWindow)
            return event; // some other window is key -- "the focused window wins"

        const bool isCommandW =
            (event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask) == NSEventModifierFlagCommand &&
            [event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"w"];
        if (!isCommandW)
            return event; // not Cmd+W -- Esc included -- let it through to the plugin's own view

        // Deferred, not synchronous: closing runs HostedPluginWindowManager::closeAllForNode,
        // which erases (and so destroys) `window` -- doing that from inside THIS block, which
        // AppKit is still executing as part of dispatching `event`, would free `window` (and this
        // Impl, and the block itself) out from under the call that's running it. callAsync hops to
        // the next message-loop turn, exactly like HostedPluginEditorWindow::instanceChanged()'s
        // own deferred recheck.
        juce::Component::SafePointer<HostedPluginEditorWindow> safeWindow(&window);
        juce::MessageManager::callAsync([safeWindow] {
            if (safeWindow != nullptr)
                safeWindow->closeButtonPressed();
        });
        return nil; // swallowed -- never reaches the plugin's own view
    }

    HostedPluginEditorWindow& window;
    id monitor = nil;
};

HostedPluginWindowMacKeyMonitor::HostedPluginWindowMacKeyMonitor(HostedPluginEditorWindow& window)
    : impl_(std::make_unique<Impl>(window)) {}

HostedPluginWindowMacKeyMonitor::~HostedPluginWindowMacKeyMonitor() = default;

} // namespace synth

#endif // JUCE_MAC
