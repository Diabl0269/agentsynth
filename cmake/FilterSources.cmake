# The Filter module and its Custom type (drawn shapes, minimum-phase kernel design, partitioned
# convolution), plus the shape-list pieces it shares with the LFO's Custom wave (Source/Modules/Shapes,
# LfoCustomShapes, LfoShapeBlender). Part of Core; its own list so the root file stays under the line cap.
target_sources(Core PRIVATE
    Source/Modules/FilterModule.h
    Source/Modules/Filter/CustomFilterShapes.h
    Source/Modules/Filter/CustomFilterShapes.cpp
    Source/Modules/Filter/CustomFilterCutoff.h
    Source/Modules/Filter/CustomFilterCutoff.cpp
    Source/Modules/Filter/CustomFilterKernelDesigner.h
    Source/Modules/Filter/CustomFilterKernelDesigner.cpp
    Source/Modules/Filter/ResonantLadder.cpp
    Source/Modules/Filter/PartitionedConvolver.h
    Source/Modules/Filter/PartitionedConvolver.cpp
    Source/Modules/Filter/CustomFilterEngine.h
    Source/Modules/Filter/CustomFilterEngine.cpp
    Source/Modules/Filter/CustomFilterEngineVoices.cpp
    Source/Modules/Filter/CustomFilterPerNote.h
    Source/Modules/Filter/CustomFilterPerNote.cpp
    Source/Modules/Filter/CustomFilterVoiceCursors.h
    Source/Modules/Filter/CustomFilterVoiceCursors.cpp
    Source/Modules/Filter/CustomFilterBlend.h
    Source/Modules/Filter/CustomFilterBlend.cpp
    Source/Modules/Filter/CustomFilterPeakMorph.h
    Source/Modules/Filter/CustomFilterPeakMorph.cpp
    Source/Modules/Shapes/LockFreeHandoff.h
    Source/Modules/Shapes/MorphShapes.h
    Source/Modules/Shapes/MorphShapes.cpp
    Source/Modules/Lfo/LfoCustomShapes.h
    Source/Modules/Lfo/LfoCustomShapes.cpp
    Source/Modules/Lfo/LfoShapeBlender.h
    Source/Modules/Lfo/LfoShapeBlender.cpp
)
