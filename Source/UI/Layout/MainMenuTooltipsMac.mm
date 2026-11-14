// Concern: the macOS half of MainMenuTooltips -- NSMenuItem.toolTip on the main menu's items, kept across the
// rebuilds JUCE does (a refill on open, a whole new menu when the model changes).
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#include "UI/Layout/MainMenuTooltips.h"

#import <AppKit/AppKit.h>

namespace synth::ui {
namespace {

MenuTooltips& tooltips() {
    static MenuTooltips table;
    return table;
}

NSString* toNS(const juce::String& s) { return [NSString stringWithUTF8String:s.toRawUTF8()]; }
juce::String fromNS(NSString* s) { return s != nil ? juce::String::fromUTF8(s.UTF8String) : juce::String(); }

// The hover hint, and the same text as the item's accessibility help so VoiceOver reads it and a script can read it
// from the live menu (macOS does not expose toolTip itself).
void setHint(NSMenuItem* item, NSString* hint) {
    item.toolTip = hint;
    item.accessibilityHelp = hint;
}

void applyTo(NSMenu* menu) {
    for (NSMenuItem* item in menu.itemArray) {
        if (item.isSeparatorItem)
            continue;
        const auto found = tooltips().find(fromNS(item.title));
        setHint(item, found != tooltips().end() ? toNS(found->second) : nil);
        if (item.submenu != nil)
            applyTo(item.submenu);
    }
}

// JUCE rebuilds the main menu in two ways: it refills a menu's items when it opens (through a delegate), and when the
// app's menu model changes it swaps in a brand-new NSMenu that has no delegate at all. Hooking the delegate misses the
// second, so the hint is set on every item as macOS reports it added, whichever way the menu was built.
void applyToAddedItem(NSNotification* note) {
    NSMenu* menu = note.object;
    NSNumber* index = note.userInfo[@"NSMenuItemIndex"];
    if (menu == nil || index == nil || index.integerValue < 0 || index.integerValue >= menu.numberOfItems)
        return;
    NSMenuItem* item = [menu itemAtIndex:index.integerValue];
    if (item.isSeparatorItem)
        return;
    const auto found = tooltips().find(fromNS(item.title));
    if (found != tooltips().end())
        setHint(item, toNS(found->second));
}

id addedItemObserver = nil;

void observeAddedItems() {
    if (addedItemObserver == nil)
        addedItemObserver = [[NSNotificationCenter defaultCenter] addObserverForName:NSMenuDidAddItemNotification
                                                                              object:nil
                                                                               queue:nil
                                                                          usingBlock:^(NSNotification* note) {
                                                                              applyToAddedItem(note);
                                                                          }];
}

} // namespace

void installMainMenuTooltips(MenuTooltips table) {
    tooltips() = std::move(table);
    NSMenu* main = NSApp.mainMenu;
    if (main == nil)
        return;
    observeAddedItems();
    for (NSMenuItem* top in main.itemArray)
        if (top.submenu != nil)
            applyTo(top.submenu);
}

void uninstallMainMenuTooltips() {
    if (addedItemObserver != nil)
        [[NSNotificationCenter defaultCenter] removeObserver:addedItemObserver];
    addedItemObserver = nil;
    tooltips().clear();
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
