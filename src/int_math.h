#include <stdint.h>

#define QER(n,b) ( ((float)n)/(1<<b) )

//
// Integer math helpers (from https://en.wikipedia.org/wiki/Q_(number_format)) ***
//
// See also:
// https://chummersone.github.io/qformat.html


int16_t f_to_q(float f, const int8_t q)
{
	return(f*((1<<q)-1));
}

float q_to_f(int16_t a, const int8_t q)
{
	return(a/(float)(1<<q));
}

float q_max_pos(int8_t q)
{
	static const int8_t q_max=sizeof(int16_t)*8-1;

	return(((1<<q_max)-1)/(float)(1<<q));
}

float q_max_neg(int8_t q)
{
	static const int8_t q_max=sizeof(int16_t)*8-1;

	return((-(1<<q_max))/(float)(1<<q));
}

int16_t sat16(int32_t x, const int8_t q)
{
	static const int8_t q_max=sizeof(int16_t)*8-1;

        if (x > (1<<q_max)-1)
                return((1<<q_max)-1);
        else if (x < -(1<<q_max))
                return(-(1<<q_max));
        else return (int16_t)x;
}

int16_t q_add(int16_t a, int16_t b, const int8_t q)
{
    return(sat16(a + b,q));
}

int16_t q_sub(int16_t a, int16_t b, const int8_t q)
{
    return(sat16(a - b,q));
}

int16_t q_mul(int16_t a, int16_t b, const int8_t q)
{
    int32_t temp;

    temp = (int32_t)a * (int32_t)b;
    temp += (1 << (q-1));

    return(sat16(temp >> q,q));
}

int16_t q_div(int16_t a, int16_t b, const int8_t q)
{
    int32_t temp = (int32_t)a << q;

    if ((temp >= 0 && b >= 0) || (temp < 0 && b < 0)) {
        temp += (b >> 1);
    } else {
        temp -= (b >> 1);
    }
    return (sat16(temp / b,q));
}
