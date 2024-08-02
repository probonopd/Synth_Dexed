#include <stdint.h>
#include <math.h>

#define Q15_SHIFT 15
#define Q15_SCALE (1 << Q15_SHIFT)

inline int16_t float_to_q15(float x) {
    return (int16_t)(x * Q15_SCALE);
}

inline float q15_to_float(int16_t x) {
    return ((float)x) / Q15_SCALE;
}

inline int16_t q15_multiply(int16_t a, int16_t b) {
    return (int16_t)(((int32_t)a * (int32_t)b) >> Q15_SHIFT);
}

inline int16_t q15_division(int16_t a, int16_t b) {
    return (int16_t)((a<<Q15_SHIFT)/b);
}


