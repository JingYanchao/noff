//
// Created by jyc on 17-4-17.
//

#ifndef NOFF_UTIL_H
#define NOFF_UTIL_H
#include <stdlib.h>
inline int before(u_int seq1, u_int seq2)
{
    return ((int)(seq1 - seq2) < 0);
}

inline int after(u_int seq1, u_int seq2)
{
    return ((int)(seq2 - seq1) < 0);
}
#endif //NOFF_UTIL_H
