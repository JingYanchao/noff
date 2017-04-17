//
// Created by root on 17-4-14.
//

#ifndef NOFF_SHARDING_H
#define NOFF_SHARDING_H

#include <netinet/ip.h>
#include <netinet/tcp.h>

class Sharding {

public:
    Sharding();

    u_int operator()(const ip* hdr, int len);
    u_int operator()(Tuple4 t, int len);

    struct Tuple4
    {
        u_int       srcIP;
        u_int       dstIP;
        u_int16_t   srcPort;
        u_int16_t   dstPort;
    };

private:
    void initRandom();

    u_char xor_[12];
    u_char perm_[12];
};

#endif //NOFF_SHARDING_H