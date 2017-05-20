//
// Created by root on 17-5-20.
//

#ifndef NOFF_PFCAPTURE_H
#define NOFF_PFCAPTURE_H

#include <pfring.h>
#include <pcap.h>
#include <functional>
#include <netinet/ip.h>
#include <vector>

class PFCapture
{
public:
public:
    typedef std::function<void(const pcap_pkthdr*, const u_char*, timeval)> PacketCallback;
    typedef std::function<void(ip *, int, timeval)> IpFragmentCallback;

    PFCapture(const std::string& deviceAndQueue, int snaplen, bool isLoopback);

    void startLoop(int packetCount);

    void addPacketCallBack(const PacketCallback& cb)
    {
        packetCallbacks_.push_back(cb);
    }

    void addIpFragmentCallback(const IpFragmentCallback& cb)
    {
        ipFragmentCallbacks_.push_back(cb);
    }

    // not thread safe, just call in signal handler
    void breakLoop();

    void setFilter(const char *filter);

private:
    pfring *cap_;

    std::string     name_;
    bool            running_;

    int             linkType_;
    const char      *linkTypeStr_ = NULL;
    int             linkOffset_;

    std::vector<PacketCallback>      packetCallbacks_;
    std::vector<IpFragmentCallback>  ipFragmentCallbacks_;

    void onPacket(const pcap_pkthdr *, const u_char *, timeval);

    void logCaptureStats();

    static void internalCallBack(const struct pfring_pkthdr *h,
                                 const u_char *p, const u_char *user);
};


#endif //NOFF_PFCAPTURE_H
