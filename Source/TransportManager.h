// Source/TransportManager.h
#pragma once
#include <atomic>
#include <juce_core/juce_core.h>

class TransportManager {
public:
    static TransportManager& getInstance();

    void play();
    void pause();
    void stop();
    bool isPlaying() const { return playing.load(); }
    void setTempo(float bpm) { tempo.store(bpm); }
    float getTempo() const { return tempo.load(); }
    void setPosition(double samples) { playheadPosition.store(samples); }
    double getPosition() const { return playheadPosition.load(); }

private:
    TransportManager() = default;
    std::atomic<bool> playing{false};
    std::atomic<float> tempo{120.0f};
    std::atomic<double> playheadPosition{0.0};
};
