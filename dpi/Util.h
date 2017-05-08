//
// Created by jyc on 17-4-17.
//

#ifndef NOFF_UTIL_H
#define NOFF_UTIL_H

#include <string>
#include <arpa/inet.h>
#include <muduo/base/Logging.h>
#include <muduo/net/InetAddress.h>

const int FROMCLIENT = 0;
const int FROMSERVER = 1;

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

    std::string toString()
    {
        std::string str;

        char buf[INET_ADDRSTRLEN];

        if (inet_ntop(AF_INET, &saddr, buf, INET_ADDRSTRLEN) == NULL) {
            LOG_FATAL << "bad IP address";
        }

        str += buf;
        str += ":";
        str += std::to_string(source) + " -> ";

        if (inet_ntop(AF_INET, &daddr, buf, INET_ADDRSTRLEN) == NULL) {
            LOG_FATAL << "bad IP address";
        }

        str += buf;
        str += ":";
        str += std::to_string(dest);

        return str;
    }

    muduo::net::InetAddress toSrcInetAddress()
    {
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(source);
        addr.sin_addr.s_addr = saddr;
        return muduo::net::InetAddress(addr);
    }

    muduo::net::InetAddress toDstInetAddress()
    {
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(dest);
        addr.sin_addr.s_addr = daddr;
        return muduo::net::InetAddress(addr);
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
