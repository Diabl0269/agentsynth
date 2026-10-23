#pragma once

#include "Modules/Shapes/ShapeLibrary.h"
#include <juce_core/juce_core.h>

namespace synth::ui {

/** Whether a Sound or a Shape is being dragged right now, app-wide. The drag source starts and ends it; every drop
 * target that shows a faint outline while one is live listens, and is told only when it starts or ends (never per mouse
 * move). Message thread only. */
class SoundDragState {
public:
    class Listener {
    public:
        virtual ~Listener() = default;
        /** A sound or shape drag started or ended: read isActive(), file() and isShapeActive(). */
        virtual void soundDragChanged() = 0;
    };

    /** A drag of `file` started; tells every listener. A second begin while one is live replaces it. */
    static void begin(const juce::File& file);
    /** A drag of a Shape of this kind, called `name`, started; tells every listener. isActive() stays false for it. */
    static void beginShape(synth::shapes::ShapeKind kind, const juce::String& name);
    /** The drag ended (dropped, cancelled or abandoned); a no-op when none is live. */
    static void end();
    /** A Sound is being dragged. */
    static bool isActive();
    /** A Shape is being dragged. */
    static bool isShapeActive();
    static synth::shapes::ShapeKind shapeKind();
    static juce::String shapeName();
    /** The file being dragged; empty when none is. */
    static juce::File file();
    static void addListener(Listener* listener);
    static void removeListener(Listener* listener);
};

} // namespace synth::ui
