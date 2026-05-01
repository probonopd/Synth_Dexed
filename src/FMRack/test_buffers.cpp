#include <iostream>
#include "../dexed.h"
#include "../synth.h"

int main() {
    std::cout << "Testing buffer sizes after optimization:" << std::endl;
    
    // Test the _N_ constant from synth.h
    std::cout << "_N_ (buffer size): " << _N_ << std::endl;
    
    // Test if we can create a Dexed instance
    try {
        // Constructor takes maxnotes and sample rate
        Dexed synth(16, 44100);  // 16 max notes, 44.1kHz sample rate
        std::cout << "Dexed synthesizer created successfully" << std::endl;
        
        // Test buffer allocation
        int16_t* testBuffer = new int16_t[_N_];
        std::cout << "Successfully allocated buffer of size " << _N_ << std::endl;
        delete[] testBuffer;
        
        // Test sample generation
        int16_t samples[_N_];
        synth.getSamples(samples, _N_);
        std::cout << "Successfully generated " << _N_ << " samples" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}