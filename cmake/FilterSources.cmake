# The Filter module and its Custom type (drawn shapes, minimum-phase kernel design, partitioned
# convolution). Part of Core; its own list so the root file stays under the line cap.
target_sources(Core PRIVATE
    Source/Modules/FilterModule.h
    Source/Modules/Filter/CustomFilterShapes.h
    Source/Modules/Filter/CustomFilterShapes.cpp
    Source/Modules/Filter/CustomFilterKernelDesigner.h
    Source/Modules/Filter/CustomFilterKernelDesigner.cpp
    Source/Modules/Filter/PartitionedConvolver.h
    Source/Modules/Filter/PartitionedConvolver.cpp
    Source/Modules/Filter/CustomFilterEngine.h
    Source/Modules/Filter/CustomFilterEngine.cpp
)
