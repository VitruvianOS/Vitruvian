/*
 * Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <ByteOrder.h>


double
__swap_double(double arg)
{
    uint64_t u;
    memcpy(&u, &arg, sizeof(u));
    u = ((u & 0x00000000000000FFULL) << 56) |
        ((u & 0x000000000000FF00ULL) << 40) |
        ((u & 0x0000000000FF0000ULL) << 24) |
        ((u & 0x00000000FF000000ULL) << 8)  |
        ((u & 0x000000FF00000000ULL) >> 8)  |
        ((u & 0x0000FF0000000000ULL) >> 24) |
        ((u & 0x00FF000000000000ULL) >> 40) |
        ((u & 0xFF00000000000000ULL) >> 56);
    memcpy(&arg, &u, sizeof(u));
    return arg;
}


float
__swap_float(float arg)
{
    uint32_t u;
    memcpy(&u, &arg, sizeof(u));
    u = ((u & 0x000000FFU) << 24) |
        ((u & 0x0000FF00U) << 8)  |
        ((u & 0x00FF0000U) >> 8)  |
        ((u & 0xFF000000U) >> 24);
    memcpy(&arg, &u, sizeof(u));
    return arg;
}
