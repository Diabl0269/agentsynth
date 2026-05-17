#include "TransportManager.h"
#include <iostream>

TransportManager& TransportManager::getInstance() {
    static TransportManager instance;
    return instance;
}

void TransportManager::play() {
    playing.store(true);
    std::cout << "TransportManager: Play" << std::endl;
}

void TransportManager::pause() {
    playing.store(false);
    std::cout << "TransportManager: Pause" << std::endl;
}

void TransportManager::stop() {
    playing.store(false);
    playheadPosition.store(0.0);
    std::cout << "TransportManager: Stop" << std::endl;
}
