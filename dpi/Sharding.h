//
// Created by root on 17-4-14.
//

#ifndef NOFF_SHARDING_H
#define NOFF_SHARDING_H

class Sharding {

public:
    Sharding();
    u_int operator()(u_int srcIP, u_int16_t srcPort, u_int dstIP, u_int16_t dstPort);

private:
    void initRandom();

    u_char xor_[12];
    u_char perm_[12];
};

#endif //NOFF_SHARDING_H