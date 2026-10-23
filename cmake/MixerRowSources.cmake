# Mixer units added alongside the lists' split: the shared Space-claiming button and the insert/send row
# helpers. Split out of AppUISources.cmake to keep that list under the file-size cap.
target_sources(AppUI PRIVATE
    Source/UI/Layout/SpaceClaimingButton.h
    Source/UI/Layout/SpaceClaimingButton.cpp
    Source/UI/Mixer/MixerInsertList/MixerInsertListDrag.cpp
)
