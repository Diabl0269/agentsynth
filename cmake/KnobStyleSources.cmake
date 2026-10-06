# Knob styles (Settings > Appearance > Controls): the style model and the five painters. In Core because
# AppLookAndFeel, whose rotary slider calls paintKnob, is compiled into Core.
target_sources(Core PRIVATE
    Source/UI/Theme/KnobStyle.h
    Source/UI/Theme/KnobStyle.cpp
    Source/UI/Theme/AppLookAndFeel/KnobPainter.h
    Source/UI/Theme/AppLookAndFeel/KnobPainterInternal.h
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelKnobStyles.cpp
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelKnobStylesAnalog.cpp
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelKnobStylesChunky.cpp
)
