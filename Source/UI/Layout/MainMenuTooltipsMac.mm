// Concern: the macOS half of MainMenuTooltips -- NSMenuItem.toolTip on the main menu's items, kept across the
// rebuild JUCE does every time a menu opens.
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#include "UI/Layout/MainMenuTooltips.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

namespace synth::ui {
namespace {

MenuTooltips& tooltips() {
    static MenuTooltips table;
    return table;
}

NSString* toNS(const juce::String& s) { return [NSString stringWithUTF8String:s.toRawUTF8()]; }
juce::String fromNS(NSString* s) { return s != nil ? juce::String::fromUTF8(s.UTF8String) : juce::String(); }

void applyTo(NSMenu* menu) {
    for (NSMenuItem* item in menu.itemArray) {
        if (item.isSeparatorItem)
            continue;
        const auto found = tooltips().find(fromNS(item.title));
        item.toolTip = found != tooltips().end() ? toNS(found->second) : nil;
        if (item.submenu != nil)
            applyTo(item.submenu);
    }
}

// JUCE's menu delegate runs menuNeedsUpdate: when a menu opens and refills it with new items; wrap that call so
// the tooltips go on the new items. One wrap per delegate class.
void wrapRefreshOf(NSMenu* menu) {
    id delegate = menu.delegate;
    if (delegate == nil)
        return;
    static NSMutableSet<NSValue*>* wrapped = [NSMutableSet set];
    Class cls = object_getClass(delegate);
    NSValue* key = [NSValue valueWithPointer:(__bridge void*)cls];
    if ([wrapped containsObject:key])
        return;
    const SEL selector = @selector(menuNeedsUpdate:);
    Method method = class_getInstanceMethod(cls, selector);
    if (method == nullptr)
        return;
    [wrapped addObject:key];
    using Original = void (*)(id, SEL, NSMenu*);
    const Original original = (Original)method_getImplementation(method);
    class_replaceMethod(cls, selector, imp_implementationWithBlock(^(id self, NSMenu* opening) {
                            original(self, selector, opening);
                            applyTo(opening);
                        }),
                        method_getTypeEncoding(method));
}

} // namespace

void installMainMenuTooltips(MenuTooltips table) {
    tooltips() = std::move(table);
    NSMenu* main = NSApp.mainMenu;
    if (main == nil)
        return;
    for (NSMenuItem* top in main.itemArray)
        if (top.submenu != nil) {
            wrapRefreshOf(top.submenu);
            applyTo(top.submenu);
        }
}

juce::String mainMenuTooltipForTest(const juce::String& menuTitle, const juce::String& itemTitle) {
    for (NSMenuItem* top in NSApp.mainMenu.itemArray) {
        NSMenu* sub = top.submenu;
        if (sub == nil || fromNS(sub.title) != menuTitle)
            continue;
        if ([sub.delegate respondsToSelector:@selector(menuNeedsUpdate:)])
            [(id<NSMenuDelegate>)sub.delegate menuNeedsUpdate:sub];
        for (NSMenuItem* item in sub.itemArray)
            if (fromNS(item.title) == itemTitle)
                return fromNS(item.toolTip);
    }
    return {};
}

} // namespace synth::ui

#endif
