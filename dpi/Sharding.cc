//
// Created by root on 17-4-14.
//

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/time.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <string.h>

#include <muduo/base/Exception.h>

#include "Sharding.h"
#include <assert.h>

namespace
{

struct Tuple4
{
    u_int       srcIP;
    u_int       dstIP;
    u_int16_t   srcPort;
    u_int16_t   dstPort;
};

Tuple4 getTuple4(const ip* hdr, int len)
{
    u_int16_t   srcPort, dstPort;
    u_char      *data = (u_char*) hdr + 4 * hdr->ip_hl;

    len -= 4 * hdr->ip_hl;
    assert(len >= 0);

    switch (hdr->ip_p)
    {

        case IPPROTO_TCP:
        {
            tcphdr *tcp = (tcphdr *) data;

            if (len < (int) sizeof(tcphdr))
            {
                throw muduo::Exception("TCP header too short");
            }

            srcPort = tcp->th_sport;
            dstPort = tcp->th_dport;
        }
            break;
        case IPPROTO_UDP: {
            udphdr *udp = (udphdr *) data;

            if (len < (int) sizeof(udphdr))
            {
                throw muduo::Exception("UDP header too short");
            }

            srcPort = udp->uh_sport;
            dstPort = udp->uh_dport;
        }
            break;
        case IPPROTO_ICMP:
            // FIXME: choose a constant port number for sharding?
            srcPort = dstPort = 928;
            break;
        default:
        {
            char str[32];
            snprintf(str, sizeof str, "unsupported protocol(0x%x)", hdr->ip_p);
            throw muduo::Exception(str);
        }
    }

    return { hdr->ip_src.s_addr,
             hdr->ip_dst.s_addr,
             srcPort,
             dstPort };
}

}


Sharding::Sharding()
{
    int i, n, j;
    int p[6];

    initRandom();
    for (i = 0; i < 6; i++)
        p[i] = i;
    for (i = 0; i < 6; i++) {
        n = perm_[i] % (6 - i);
        perm_[i] = (u_char) p[n];

        for (j = 0; j < 5 - n; j++)
            p[n + j] = p[n + j + 1];
    }
}

u_int Sharding::operator()(const ip* hdr, int len)
{
    u_int   res = 0;
    u_char  data[6];

    Tuple4 t = getTuple4(hdr, len);;



    u_int flag1 = t.dstIP^t.srcIP;
    u_short flag2 = t.dstPort^t.srcPort;
    memmove(data, &flag1, 4);
    memmove(data+4,&flag2,2);
    for (int i = 0; i < 6; i++)
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
        read(fd, xor_, 6);
        read(fd, perm_, 6);
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