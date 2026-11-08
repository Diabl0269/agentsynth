#pragma once

// PopoverContent.h -- what popover content implements to take part in the shared popover path (PopupHost.h). Kept apart
// so a content class names its base without including the launch machinery.

namespace synth::ui {

class AppUndoAware {
public:
    virtual ~AppUndoAware() = default;
    /** The app is about to run undo or redo (the key has just been handed over): put back anything the popover has
     *  changed live and not recorded, so the undo works on the project as it was. */
    virtual void appAboutToUndoOrRedo() {}
    /** The app has just run undo or redo. Re-read what is shown, or close the popover without applying anything. */
    virtual void appUndoneOrRedone() = 0;
};

/** Mixin for popover content that changes things live and must put them back when the user presses Escape (the colour
 *  picker restores its colour). A click away commits, so only Escape calls it. */
class PopoverContent {
public:
    virtual ~PopoverContent() = default;
    virtual void onPopoverCancel() {}
};

} // namespace synth::ui
