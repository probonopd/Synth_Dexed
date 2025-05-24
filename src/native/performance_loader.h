#pragma once
#include <string>
#include "StereoDexed.h"

// Loads a Soundplantage-style .ini performance file and applies it to the given Dexed synth instance.
bool load_performance_ini(const std::string& filename, StereoDexed* synth, int partIndex = 1, bool debug = false);
