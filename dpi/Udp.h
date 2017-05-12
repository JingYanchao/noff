//
// Created by root on 17-4-26.
//

#ifndef DNSPARSER_UDP_H
#define DNSPARSER_UDP_H

#include "Util.h"
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <functional>
#include <vector>
class Udp
{
public:
    typedef std::function<void(tuple4,char*,int,timeval)> UdpCallback;

    void addUdpCallback(UdpCallback cb)

    {
        udpCallback_.push_back(cb);
    }
    void processUdp(ip* iphdr,int skblen,timeval);
private:
    std::vector<UdpCallback> udpCallback_;

};

#endif //DNSPARSER_UDP_H