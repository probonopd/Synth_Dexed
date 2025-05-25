#pragma once
#include <iostream>

// Global debug flag
extern bool debugEnabled;

// Debug print macro
#define DEBUG_PRINT(x) do { if (debugEnabled) { std::cout << x << std::endl; } } while(0)

// Declare multiprocessingEnabled as extern so it can be shared between main.cpp and Rack.cpp
extern int multiprocessingEnabled;
