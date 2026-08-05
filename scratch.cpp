#include <iostream>
#include <cmath>
#include <random>
#include <algorithm>

int main() {
    std::mt19937 gen(123);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    
    auto test = [&](float color, int type) {
        float b0=0, b1=0, b2=0, b3=0, b4=0, b5=0, b6=0, brown=0, lastOutput=0;
        float sampleRate = 44100.0f;
        int clips = 0;
        int n = 44100 * 4;
        float rms = 0;
        for(int i=0; i<n; ++i) {
            float white = dist(gen);
            float out = white;
            if (type == 1) { // Pink
                b0 = 0.99886f * b0 + white * 0.0555179f;
                b1 = 0.99332f * b1 + white * 0.0750759f;
                b2 = 0.96900f * b2 + white * 0.1538520f;
                b3 = 0.86650f * b3 + white * 0.3104856f;
                b4 = 0.55000f * b4 + white * 0.5329522f;
                b5 = -0.7616f * b5 - white * 0.0168980f;
                out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
                b6 = white * 0.115926f;
                out *= 0.11f; 
            } else if (type == 2) { // Brown
                brown = (brown + (0.02f * white)) / 1.02f;
                out = brown * 3.0f; 
            }
            
            if (color > 0.01f) {
                float fc = 20.0f * std::pow(1000.0f, color); 
                float omega = 2.0f * 3.1415926535f * fc / sampleRate;
                float alpha = std::exp(-omega);
                lastOutput = alpha * lastOutput + (1.0f - alpha) * out;
                out = out - lastOutput;
                float makeUp = std::sqrt((1.0f + alpha) / std::max(1e-6f, 2.0f * alpha * alpha));
                if (type == 1) makeUp *= 1.5f;
                if (type == 2) makeUp *= 3.0f;
                out *= std::min(makeUp, 4.0f);
            } else if (color < -0.01f) {
                float fc = 20000.0f * std::pow(0.001f, -color); 
                float omega = 2.0f * 3.1415926535f * fc / sampleRate;
                float alpha = std::exp(-omega);
                lastOutput = alpha * lastOutput + (1.0f - alpha) * out;
                out = lastOutput;
                float makeUp = std::sqrt((1.0f + alpha) / std::max(1e-6f, 1.0f - alpha));
                if (type == 1) makeUp = std::pow(makeUp, 0.5f);
                if (type == 2) makeUp = 1.0f;
                out *= std::min(makeUp, 4.0f);
            } else {
                lastOutput *= 0.99f;
            }
            rms += out * out;
            if (std::abs(out) > 1.0f) clips++;
        }
        rms = std::sqrt(rms / n);
        std::cout << "Type: " << type << " Color: " << color << " RMS: " << rms << " Clips: " << (float)clips / n * 100.0f << "%\n";
    };
    
    test(0.0f, 0); // White
    test(0.0f, 1); // Pink
    test(0.0f, 2); // Brown
    test(-1.0f, 0); // White LP
    test(1.0f, 0); // White HP
    
    test(-1.0f, 1); // Pink LP
    test(1.0f, 1); // Pink HP

    test(-1.0f, 2); // Brown LP
    test(1.0f, 2); // Brown HP

    test(-0.4f, 0); // White LP mid
    test(-0.7f, 0); // White LP mid
    return 0;
}
