//
// Created by root on 17-4-14.
//

#ifndef NOFF_SHARDING_H
#define NOFF_SHARDING_H

#include <netinet/ip.h>

class Sharding {

public:
    Sharding();

    u_int operator()(const ip* hdr, int len);

private:
    void initRandom();

    u_char xor_[6];
    u_char perm_[6];
};

#endif //NOFF_SHARDING_H