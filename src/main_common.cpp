#include <iostream>
#include "native/main_common.h"

// Renamed this function so it doesn't conflict with the one in native/main_common.cpp
void debug_audio_buffers(bool useSynth) {
    // Define bufferIndex locally - using a default value of 0
    static int bufferIndex = 0;
    
    // Debug code only - this function isn't called in the actual program
    std::cout << "[DEBUG] debug_audio_buffers: synth=" << synth << std::endl;
    if (synth) {
        synth->getSamples(audioBuffers[bufferIndex].data(), BUFFER_FRAMES);
        std::cout << "[DEBUG] getSamples called, first sample: " << audioBuffers[bufferIndex][0] << std::endl;
        
        // Increment bufferIndex for next call, wrapping around if needed
        bufferIndex = (bufferIndex + 1) % numBuffers;
    } else {
        std::cout << "[DEBUG] synth is nullptr!" << std::endl;
    }
}