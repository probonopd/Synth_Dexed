#pragma once
#include <inttypes.h>
#include <limits.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef int16_t q16_t;
typedef int32_t q32_t;

#define Q16_SHIFT 11
#define Q32_SHIFT 27

#define Q16_COS_LUT_SIZE 32
#define Q32_COS_LUT_SIZE 32

#if Q16_SHIFT > 13
#error Q16_M_PI is not pre-calculated for this fixed point shift
#endif
#define Q16_M_PI(M) 25735 >> (13 - M)           // use max of 13 fractional bits!
#define Q16_M_TWO_PI(M) Q16_M_PI((M) - 1)
#define Q16_M_PI_HALF(M) Q16_M_PI((M) + 1)
q16_t __q16_cos_lut[Q16_COS_LUT_SIZE];

#if Q32_SHIFT > 29 
#error Q32_M_PI is not pre-calculated for this fixed point shift
#endif
#define Q32_M_PI(M) 1686629760 >> (29 - M)      // use max 29 fractional bits!
#define Q32_M_TWO_PI(M) Q32_M_PI((M) - 1)
#define Q32_M_PI_HALF(M) Q32_M_PI((M) + 1)
q32_t __q32_cos_lut[Q32_COS_LUT_SIZE];

inline q16_t float_to_q16(float x, uint8_t M) {
  return (q16_t(x * (1 << M)));
}

inline float q16_to_float(q16_t x, uint8_t M) {
  return (float(x) / (1 << M));
}

inline q16_t q16_convert(q16_t input, uint8_t src_frac_bits, uint8_t dest_frac_bits) {
  if (src_frac_bits > dest_frac_bits) {
    return input >> (src_frac_bits - dest_frac_bits);
  } else if (src_frac_bits < dest_frac_bits) {
    return input << (dest_frac_bits - src_frac_bits);
  }
  return input;
}

inline q16_t q16_saturate(q16_t r) {
    if (r > INT16_MAX)
        return INT16_MAX;
    else if (r < INT16_MIN)
        return INT16_MIN;
    else
        return(q16_t(r));
}

inline q16_t q16_add(q16_t a, q16_t b) {
    return(a + b);
}

inline q16_t q16_add_sat(q16_t a, q16_t b) {
    int32_t r = a + b;
    return(q16_saturate(r));
}

inline q16_t q16_sub(q16_t a, q16_t b) {
    return(a - b);
}

inline q16_t q16_sub_sat(q16_t a, q16_t b) {
    int32_t r = a - b;
    return(q16_saturate(r));
}

inline q16_t q16_mul(q16_t a, q16_t b, uint8_t M) {
    int32_t r = (((int32_t)a * (int32_t)b) >> M);
    return(r);
}

inline q16_t q16_mul_sat(q16_t a, q16_t b, uint8_t M) {
    int32_t r = (((int32_t)a * (int32_t)b) >> M);
    return(q16_saturate(r));
}

inline q16_t q16_div(q16_t a, q16_t b, uint8_t M) {
    if (b == 0) {
        return (a >= 0) ? INT16_MAX : INT16_MIN;
    }
    int32_t r = ((int32_t)a << M) / (int32_t)b;
    return(r);
}

inline q16_t q16_div_sat(q16_t a, q16_t b, uint8_t M) {
    if (b == 0) {
        return (a >= 0) ? INT16_MAX : INT16_MIN;
    }
    int32_t r = ((int32_t)a << M) / (int32_t)b;
    return(q16_saturate(r));
}

int16_t q16_linear_interpolate(int16_t y0, int16_t y1, int16_t x, int16_t x0, int16_t x1) {
    int16_t x0_sub_from_x1 = x1 - x0;

    if(x0_sub_from_x1 == 0)
        return(y0);
    else
        return(y0 + ((y1 - y0) * (x - x0) / x0_sub_from_x1));
}

void q16_init_cos(uint8_t M) {
  float angle;

  for(uint16_t i = 0; i < Q16_COS_LUT_SIZE; ++i) {
    angle = M_PI/2.0 * i / (Q16_COS_LUT_SIZE - 1);
    __q16_cos_lut[i] = float_to_q16(cos(angle), M);
  }
}

inline q16_t q16_cos(q16_t x, uint8_t M) {
    q16_t angle = x % (Q16_M_TWO_PI(M)); 

    if (angle < 0) {
        angle += (Q16_M_TWO_PI(M));
    }

    uint8_t quadrant = angle / (Q16_M_PI_HALF(M));
    angle = angle % (Q16_M_PI_HALF(M));
   if (quadrant == 1 || quadrant == 3)
        angle = (Q16_M_PI_HALF(M)) - angle;


    uint16_t index = (int32_t)angle * Q16_COS_LUT_SIZE / (Q16_M_PI_HALF(M));
    uint16_t next_index = index + 1;

    if (next_index >= Q16_COS_LUT_SIZE) {
        next_index = Q16_COS_LUT_SIZE - 1;
    }

    q16_t x0 = (Q16_M_PI_HALF(M)) * index / (Q16_COS_LUT_SIZE - 1);
    q16_t x1 = (Q16_M_PI_HALF(M)) * next_index / (Q16_COS_LUT_SIZE - 1);

    int16_t x0_sub_from_x1 = x1 - x0;
    q16_t result = __q16_cos_lut[index];

    if(x0_sub_from_x1 != 0)
        result=__q16_cos_lut[index] + ((__q16_cos_lut[next_index] - __q16_cos_lut[index]) * (angle - x0) / x0_sub_from_x1);

    //q16_t result = q16_linear_interpolate(__q16_cos_lut[index], __q16_cos_lut[next_index], angle, x0, x1);

    if (quadrant == 1 || quadrant == 2) {
      result = -result;
    }

    return(result);
}

inline q16_t q16_sin(q16_t x, uint8_t M) {
    return(q16_cos(q16_sub((Q16_M_PI_HALF(M)), x),M));
}

// 32 bit fixed point
inline q32_t float_to_q32(float x, uint8_t M) {
  return (q32_t(x * (1 << M)));
}

inline float q32_to_float(q32_t x, uint8_t M) {
  return (float(x) / (1 << M));
}

inline q32_t q32_convert(q32_t input, uint8_t src_frac_bits, uint8_t dest_frac_bits) {
  if (src_frac_bits > dest_frac_bits) {
    return input >> (src_frac_bits - dest_frac_bits);
  } else if (src_frac_bits < dest_frac_bits) {
    return input << (dest_frac_bits - src_frac_bits);
  }
  return(input);
}

inline q32_t q32_saturate(q32_t r) {
    if (r > INT32_MAX)
        return INT32_MAX;
    else if (r < INT32_MIN)
        return INT32_MIN;
    else
        return(q32_t(r));
}

inline q32_t q32_add(q32_t a, q32_t b) {
    return(a + b);
}

inline q32_t q32_add_sat(q32_t a, q32_t b) {
    int32_t r = a + b;
    return(q32_saturate(r));
}

inline q32_t q32_sub(q32_t a, q32_t b) {
    return(a - b);
}

inline q32_t q32_sub_sat(q32_t a, q32_t b) {
    int32_t r = a - b;
    return(q32_saturate(r));
}

inline q32_t q32_mul(q32_t a, q32_t b, uint8_t M) {
    int32_t r = (((int64_t)a * (int64_t)b) >> M);
    return(r);
}

inline q32_t q32_mul_sat(q32_t a, q32_t b, uint8_t M) {
    int32_t r = (((int64_t)a * (int64_t)b) >> M);
    return(q32_saturate(r));
}

inline q32_t q32_div(q32_t a, q32_t b, uint8_t M) {
    if (b == 0) {
        return (a >= 0) ? INT32_MAX : INT32_MIN;
    }
    int32_t r = ((int64_t)a << M) / (int64_t)b;
    return(r);
}

inline q32_t q32_div_sat(q32_t a, q32_t b, uint8_t M) {
    if (b == 0) {
        return (a >= 0) ? INT32_MAX : INT32_MIN;
    }
    int32_t r = ((int64_t)a << M) / (int64_t)b;
    return(q32_saturate(r));
}

int32_t q32_linear_interpolate(int32_t y0, int32_t y1, int32_t x, int32_t x0, int32_t x1) {
    int32_t x0_sub_from_x1 = x1 - x0;

    if(x0_sub_from_x1 == 0)
        return(y0);
    else
        return(y0 + ((y1 - y0) * (x - x0) / x0_sub_from_x1));
}

void q32_init_cos(uint8_t M) {
  float angle;

  for(uint16_t i = 0; i < Q32_COS_LUT_SIZE; ++i) {
    angle = M_PI/2.0 * i / (Q32_COS_LUT_SIZE - 1);
    __q32_cos_lut[i] = float_to_q32(cos(angle), M);
  }
}

inline q32_t q32_cos(q32_t x, uint8_t M) {
    q32_t angle = x % (Q32_M_TWO_PI(M)); 

    if (angle < 0) {
        angle += (Q32_M_TWO_PI(M));
    }

    uint8_t quadrant = angle / (Q32_M_PI_HALF(M));
    angle = angle % (Q32_M_PI_HALF(M));
    if (quadrant == 1 || quadrant == 3)
        angle = (Q32_M_PI_HALF(M)) - angle;


    uint16_t index = (int64_t)angle * Q32_COS_LUT_SIZE / (Q32_M_PI_HALF(M));
    uint16_t next_index = index + 1;

    if (next_index >= Q32_COS_LUT_SIZE) {
        next_index = Q32_COS_LUT_SIZE - 1;
    }

    q32_t x0 = (Q32_M_PI_HALF(M)) * index / (Q32_COS_LUT_SIZE - 1);
    q32_t x1 = (Q32_M_PI_HALF(M)) * next_index / (Q32_COS_LUT_SIZE - 1);

    q32_t result = q32_linear_interpolate(__q32_cos_lut[index], __q32_cos_lut[next_index], angle, x0, x1);

    if (quadrant == 1 || quadrant == 2) {
      result = -result;
    }

    return(result);
}

inline q32_t q32_sin(q32_t x, uint8_t M) {
    return(q32_cos(q32_sub((Q32_M_PI_HALF(M)), x),M));
}
