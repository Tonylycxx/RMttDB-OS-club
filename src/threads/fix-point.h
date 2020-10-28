#include <stdint.h>

#define FIX_POINT_FLAG_P 17
#define FIX_POINT_FLAG_Q 31 - FIX_POINT_FLAG_P

#define FIX_POINT_FLAG_F 2 << FIX_POINT_FLAG_Q

#define int2fixed(n) n *FIX_POINT_FLAG_F
#define fixed2int_toward_zero(x) x / FIX_POINT_FLAG_F
#define fixed2int_to_nearest(x) x < 0 ? (x - FIX_POINT_FLAG_F / 2) / FIX_POINT_FLAG_F : (x + FIX_POINT_FLAG_F / 2) / FIX_POINT_FLAG_F

#define add_fixed_fixed(x, y) x + y
#define sub_fixed_fixed(x, y) x - y

#define add_fixed_int(x, n) x + n *FIX_POINT_FLAG_F
#define sub_fixed_int(x, n) x - n *FIX_POINT_FLAG_F

#define mul_fixed_fixed(x, y) ((int64_t)x) * y / FIX_POINT_FLAG_F
#define mul_fixed_int(x, n) x *n

#define div_fixed_fixed(x, y) ((int64_t)x) * f / y
#define div_fixed_int(x, n) x / n