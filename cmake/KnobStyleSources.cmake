# Knob styles (Settings > Appearance > Knobs): the style model and the six painters. In Core because
# AppLookAndFeel, whose rotary slider calls paintKnob, is compiled into Core.
target_sources(Core PRIVATE
    Source/UI/Theme/KnobStyle.h
    Source/UI/Theme/KnobStyle.cpp
    Source/UI/Theme/AppLookAndFeel/KnobPainter.h
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelKnobStyles.cpp
)
