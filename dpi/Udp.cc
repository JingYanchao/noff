//
// Created by root on 17-4-26.
//
#include "Udp.h"
#include "Util.h"
#include <muduo/base/Logging.h>
void Udp::processUdp(ip *iphdr, int skblen, timeval timeStamp)
{
    ip* this_iphdr = iphdr;
    int datalen,iplen;
    iplen = ntohs(this_iphdr->ip_len);
    datalen = iplen - 4 * this_iphdr->ip_hl - sizeof(udphdr);
    if(datalen<0)
        LOG_WARN<<"udphdr is invalid";
    if(datalen == 0)
        LOG_DEBUG<<"udp has no data";
    tuple4 udptuple;
    udphdr* this_udphdr = (udphdr *)((u_char*)iphdr + 4 * this_iphdr->ip_hl);
    udptuple.saddr = this_iphdr->ip_src.s_addr;
    udptuple.daddr = this_iphdr->ip_dst.s_addr;
    udptuple.source = ntohs(this_udphdr->source);
    udptuple.dest = ntohs(this_udphdr->dest);
    for(auto& func:udpCallback_)
    {
        func(udptuple,((char*)this_udphdr+sizeof(udphdr)),datalen,timeStamp);
    }
}