//
// Created by jyc on 17-4-17.
//

#ifndef NOFF_UTIL_H
#define NOFF_UTIL_H
#include <stdlib.h>

struct tuple4
{
    u_short source;
    u_short dest;
    u_int saddr;
    u_int daddr;

    tuple4(u_short srcPort, u_short dstPort, u_int srcIP, u_int dstIP):
        source(srcPort), dest(dstPort), saddr(srcIP), daddr(dstIP)
    {}

    tuple4(){}

    bool operator == (const tuple4 rhs)const
    {
        return source == rhs.source &&
               dest == rhs.dest &&
               saddr == rhs.saddr &&
               daddr == rhs.daddr;
    }
};

inline int before(u_int seq1, u_int seq2)
{
    return ((int)(seq1 - seq2) < 0);
}

inline int after(u_int seq1, u_int seq2)
{
    return ((int)(seq2 - seq1) < 0);
}
#endif //NOFF_UTIL_H
