//
// Created by root on 17-5-9.
//

#ifndef DNSPARSER_MACCOUNT_H
#define DNSPARSER_MACCOUNT_H

#include <util/Timer.h>
#include <muduo/base/noncopyable.h>
#include <functional>
#include <vector>

//const char MAC1[7] = {(char)0xe4, (char)0xc7, 0x22, 0x3a, 0x06, (char)0xb5};
//const char MAC2[7]={(char) 0x84, 0x78, (char) 0xac, 0x61, 0x22, (char) 0xf1};
const u_int8_t MAC1[6] = {228,199,34,62,5,248};
const u_int8_t MAC2[6] = {132,120,172,97,34,241};

struct MacInfo
{
    long long int inputstream;
    long long int outputstream;
};


class MacCount: muduo::noncopyable
{

public:
    typedef std::function<void(MacInfo&)> EtherCallback;
    MacCount()
    {
        macInfo.inputstream = 0;
        macInfo.outputstream = 0;
    };
    void addEtherCallback(const EtherCallback& cb)
    {
        etherCallback_.push_back(cb);
    }
    int macCompare(u_int8_t* macArray);
    void processMac(const pcap_pkthdr*, const u_char*, timeval);
private:
    std::vector<EtherCallback> etherCallback_;
    Timer timer;
    MacInfo macInfo;
};

std::string to_string(const MacInfo& macInfo);


#endif //DNSPARSER_MACCOUNT_H
