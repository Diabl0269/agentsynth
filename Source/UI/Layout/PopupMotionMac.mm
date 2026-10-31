// Concern: the macOS half of PopupMotion -- a picture of a whole native window, title bar included,
// for the leaving ghost of a dialog, and turning off AppKit's own show/hide animation of a popup window.
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#include <juce_gui_basics/juce_gui_basics.h>

#import <AppKit/AppKit.h>

namespace synth::ui {

juce::Image captureNativeWindowImage(void* nativeView, int& topInset) {
    topInset = 0;
    NSView* view = (__bridge NSView*)nativeView;
    NSWindow* window = view.window;
    if (window == nil)
        return {};

    NSView* frameView = window.contentView.superview != nil ? window.contentView.superview : window.contentView;
    const NSRect bounds = frameView.bounds;
    NSBitmapImageRep* rep = [frameView bitmapImageRepForCachingDisplayInRect:bounds];
    if (rep == nil)
        return {};
    [frameView cacheDisplayInRect:bounds toBitmapImageRep:rep];

    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if (png == nil)
        return {};

    topInset = juce::jmax(0, (int)std::lround(bounds.size.height - window.contentView.frame.size.height));
    return juce::ImageFileFormat::loadFrom(png.bytes, (size_t)png.length);
}

// AppKit animates a popup window out by itself when it is ordered out (a fade and a slight shrink, ~200 ms).
// PopupMotion has already faded the window by then; measured on the real app, the app window's own frames did
// not reach the screen while AppKit's animation ran, so a control growing or shrinking behind it froze.
void disableNativeWindowAnimation(void* nativeView) {
    NSView* view = (__bridge NSView*)nativeView;
    if (NSWindow* window = view.window)
        window.animationBehavior = NSWindowAnimationBehaviorNone;
}

bool nativeWindowAnimationIsOff(void* nativeView) {
    NSView* view = (__bridge NSView*)nativeView;
    return view.window != nil && view.window.animationBehavior == NSWindowAnimationBehaviorNone;
}

// A child window stays above its parent through anything AppKit does to the parent, and moves with it.
void keepNativeWindowAbove(void* popupView, void* ownerView) {
    NSWindow* popup = ((__bridge NSView*)popupView).window;
    NSWindow* owner = ((__bridge NSView*)ownerView).window;
    if (popup == nil || owner == nil || popup == owner || popup.parentWindow != nil)
        return;
    [owner addChildWindow:popup ordered:NSWindowAbove];
}

// Front-to-back position in the application's window list: smaller is nearer the viewer. Levels count.
bool nativeWindowIsAbove(void* upperView, void* lowerView) {
    NSWindow* upper = ((__bridge NSView*)upperView).window;
    NSWindow* lower = ((__bridge NSView*)lowerView).window;
    if (upper == nil || lower == nil)
        return false;
    for (NSWindow* parent = upper.parentWindow; parent != nil; parent = parent.parentWindow)
        if (parent == lower)
            return true; // a child window is above its parent whatever the list says
    NSArray<NSWindow*>* ordered = [NSApp orderedWindows];
    const NSUInteger upperIndex = [ordered indexOfObject:upper];
    const NSUInteger lowerIndex = [ordered indexOfObject:lower];
    if (upperIndex == NSNotFound || lowerIndex == NSNotFound)
        return upper.level > lower.level;
    return upperIndex < lowerIndex;
}

} // namespace synth::ui

#endif
