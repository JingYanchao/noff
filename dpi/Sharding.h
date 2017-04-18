//
// Created by root on 17-4-14.
//

#ifndef NOFF_SHARDING_H
#define NOFF_SHARDING_H

#include <netinet/ip.h>
#include <netinet/tcp.h>

#include "Util.h"

class Sharding {

public:
    Sharding();

    u_int operator()(const ip* hdr, int len)const;
    u_int operator()(tuple4 t) const;

private:
    void initRandom();

    u_char xor_[12];
    u_char perm_[12];
};

#endif //NOFF_SHARDING_H