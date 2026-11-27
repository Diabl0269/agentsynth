// Concern: the multi-core render switch on the engine (the worker path itself lives in TrackWorkerRender).
#include "AudioEngine.h"
#include "AudioEngine/TrackWorkerRender.h"

// Off by default. Only the standalone device callback ever renders through the workers (renderPass checks
// renderingDeviceBlock_), so a bounce or a hosted block keeps JUCE's single-thread graph and its byte-identical
// output. Switching while a device runs swaps the pool and plan in with the device still running.
void AudioEngine::setTrackWorkerCount(int workers) { trackWorkers_->setWorkerCount(workers, mainProcessorGraph); }

int AudioEngine::getTrackWorkerCount() const noexcept { return trackWorkers_->workerCount(); }

bool AudioEngine::isTrackWorkerRenderActive() const noexcept { return trackWorkers_->isActive(); }
