#pragma once

#include <juce_core/juce_core.h>

#if JUCE_MAC

#include <memory>

namespace synth {

class HostedPluginEditorWindow;

// RAII wrapper around a local NSEvent keyDown monitor scoped to one
// HostedPluginEditorWindow's own NSWindow, handling Cmd+W only — see HostedPluginWindowMacKeyMonitor.mm
// for the rationale (Cmd+W is a key equivalent the app's own menus would otherwise claim before
// AppKit's normal responder-chain delivery ever reaches a plugin's own native NSView editor; Esc is
// deliberately left to that responder chain instead) and the Cocoa-side implementation. This header
// stays plain C++ (a
// second PIMPL layer, `Impl`) so HostedPluginEditorWindow.cpp — a normal .cpp, compiled on every
// platform — can hold a `std::unique_ptr<HostedPluginWindowMacKeyMonitor>` member and destroy it
// without ever needing Objective-C++ syntax itself.
class HostedPluginWindowMacKeyMonitor {
public:
    // `window` must outlive this monitor — HostedPluginEditorWindow only ever constructs this as
    // its own member, so that's automatic.
    explicit HostedPluginWindowMacKeyMonitor(HostedPluginEditorWindow& window);
    ~HostedPluginWindowMacKeyMonitor();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostedPluginWindowMacKeyMonitor)
};

} // namespace synth

#endif // JUCE_MAC
