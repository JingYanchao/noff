//
// Created by root on 17-4-14.
//

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/time.h>

#include "Sharding.h"

Sharding::Sharding()
{
    int i, n, j;
    int p[12];

    initRandom();
    for (i = 0; i < 12; i++)
        p[i] = i;
    for (i = 0; i < 12; i++) {
        n = perm_[i] % (12 - i);
        perm_[i] = (u_char) p[n];

        for (j = 0; j < 11 - n; j++)
            p[n + j] = p[n + j + 1];
    }
}

u_int Sharding::operator()(u_int srcIP, u_int16_t srcPort, u_int dstIP, u_int16_t dstPort)
{
    u_int   res = 0;
    int     i;
    u_char  data[12];

    u_int *stupid_strict_aliasing_warnings=(u_int*)data;
    *stupid_strict_aliasing_warnings = srcIP;

    *(u_int *) (data + 4) = dstIP;
    *(u_int16_t *) (data + 8) = srcPort;
    *(u_int16_t *) (data + 10) = dstPort;

    for (i = 0; i < 12; i++)
        res = ( (res << 8) + (data[perm_[i]] ^ xor_[i])) % 0xff100f;

    return res;
}

void Sharding::initRandom()
{
    struct  timeval s;
    u_int   *ptr;
    int     fd;

    fd = open("/dev/urandom", O_RDONLY);
    if (fd > 0) {
        read(fd, xor_, 12);
        read(fd, perm_, 12);
        close(fd);
        return;
    }

    gettimeofday(&s, 0);

    srand((unsigned int)s.tv_usec);

    ptr = (u_int*) xor_;
    *ptr = (u_int) rand();
    *(ptr + 1) = (u_int) rand();
    *(ptr + 2) = (u_int) rand();

    ptr = (u_int*) perm_;
    *ptr = (u_int) rand();
    *(ptr + 1) = (u_int) rand();
    *(ptr + 2) = (u_int) rand();
}