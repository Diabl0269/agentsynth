// Concern: the macOS half of PopupMotion -- a picture of a whole native window, title bar included,
// for the leaving ghost of a dialog.
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

} // namespace synth::ui

#endif
