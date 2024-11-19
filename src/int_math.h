#include <stdint.h>
#include <math.h>
#include <limits.h>

// Q0.15
#define Q0_15_SHIFT 15
#define Q0_15_SCALE (1 << Q0_15_SHIFT)

inline int16_t float_to_q0_15(float x) {
    return (int16_t)(x * Q0_15_SCALE);
}

inline float q0_15_to_float(int16_t x) {
    return ((float)x) / Q0_15_SCALE;
}

inline int16_t q0_15_multiply(int16_t a, int16_t b) {
    return (int16_t)(((int32_t)a * (int32_t)b) >> Q0_15_SHIFT);
}

inline int16_t q0_15_division(int16_t a, int16_t b) {
    return (int16_t)((a<<Q0_15_SHIFT)/b);
}

inline int16_t q0_15_division_round(int16_t a, int16_t b) {
    if(a%b>(a/(b<<1)))
    	return (int16_t)((a<<Q0_15_SHIFT)/b++);
    else
    	return (int16_t)((a<<Q0_15_SHIFT)/b);
}

// Q23.8
#define Q23_8_SHIFT 8
#define Q23_8_SCALE (1 << Q23_8_SHIFT)

int32_t float_to_q23_8(float x) {
    return (int32_t)(x * Q23_8_SCALE);
}

float q23_8_to_float(int32_t x) {
    return ((float)x) / Q23_8_SCALE;
}

int32_t q23_8_multiply(int32_t a, int32_t b) {
    return (int32_t)((((int64_t)a * (int64_t)b) >> Q23_8_SHIFT));
}

inline int16_t q23_8_division(int32_t a, int32_t b) {
    return (int32_t)((a<<Q23_8_SHIFT)/b);
}

inline int16_t q23_8_division_round(int32_t a, int32_t b) {
    if(a%b>(a/(b<<1)))
    	return (int32_t)((a<<Q23_8_SHIFT)/b++);
    else
    	return (int32_t)((a<<Q23_8_SHIFT)/b);
}

static inline int16_t q23_8_to_q0_15_with_saturation(int32_t q23_8_value) {
    int32_t q0_15_value = q23_8_value >> (Q23_8_SHIFT - 1);

    if (q0_15_value > std::numeric_limits<int16_t>::max()) {
        q0_15_value = std::numeric_limits<int16_t>::max();
    } else if (q0_15_value < std::numeric_limits<int16_t>::min()) {
        q0_15_value = std::numeric_limits<int16_t>::min();
    }

    return static_cast<int16_t>(q0_15_value);
}
