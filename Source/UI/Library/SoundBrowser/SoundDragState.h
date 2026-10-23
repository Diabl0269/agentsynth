#pragma once

#include <juce_core/juce_core.h>

namespace synth::ui {

/** Whether a Sound is being dragged right now, app-wide. The drag source starts and ends it; every drop target that
 *  shows a faint outline while one is live listens, and is told only when it starts or ends (never per mouse move).
 *  Message thread only. */
class SoundDragState {
public:
    class Listener {
    public:
        virtual ~Listener() = default;
        /** A sound drag started or ended: read isActive() and file(). */
        virtual void soundDragChanged() = 0;
    };

    /** A drag of `file` started; tells every listener. A second begin while one is live replaces it. */
    static void begin(const juce::File& file);
    /** The drag ended (dropped, cancelled or abandoned); a no-op when none is live. */
    static void end();
    static bool isActive();
    /** The file being dragged; empty when none is. */
    static juce::File file();
    static void addListener(Listener* listener);
    static void removeListener(Listener* listener);
};

} // namespace synth::ui
