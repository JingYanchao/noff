//
// Created by root on 17-5-12.
//

#include "TcpHeader.h"
#include <netinet/tcp.h>
#include <string>
#include <arpa/inet.h>

void TcpHeader::processTcpHeader(ip * data,int skblen, timeval timeStamp)
{
    ip* this_iphdr = data;
    tcphdr *this_tcphdr = (tcphdr *)((u_char*)data + 4 * this_iphdr->ip_hl);
    tcpheader hdr;
    hdr.srcIP = this_iphdr->ip_src.s_addr;
    hdr.dstIP = this_iphdr->ip_dst.s_addr;
    hdr.srcPort = htons(this_tcphdr->source);
    hdr.dstPort = htons(this_tcphdr->dest);
    hdr.flag = this_tcphdr->th_flags;
    for(const auto& func:tcpHeaderCallback_)
    {
        func(hdr);
    }
}

std::string to_string(const tcpheader& hdr)
{
    char temp[30];
    std::string res;
    char src_ip[30];
    inet_ntop(AF_INET,&hdr.srcIP,src_ip,30);
    res.append(src_ip);
    res.append("\t");
    char dst_ip[30];
    inet_ntop(AF_INET,&hdr.dstIP,dst_ip,30);
    res.append(dst_ip);
    res.append("\t");
    sprintf(temp,"%u",hdr.srcPort);
    res.append(temp);
    res.append("\t");
    sprintf(temp,"%u",hdr.dstPort);
    res.append(temp);
    res.append("\t");
    sprintf(temp,"%u",hdr.flag);
    res.append(temp);
    return res;

}