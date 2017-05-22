//
// Created by root on 17-5-20.
//

#include <muduo/base/Logging.h>
#include <pfring.h>
#include "PFCapture.h"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;


PFCapture::PFCapture(const std::string &deviceAndQueue, int snaplen, bool isLoopback)
        :running_(false),
         name_(deviceAndQueue)
{
    cap_ = pfring_open(deviceAndQueue.c_str(), snaplen, PF_RING_PROMISC);
    if (cap_ == NULL) {
        LOG_FATAL << "open " << deviceAndQueue << "failed";
    }

    if (pfring_enable_ring(cap_) != 0) {
        LOG_FATAL << "enable ring " << deviceAndQueue <<"error";
    }

    linkType_ = isLoopback ? DLT_LINUX_SLL : DLT_EN10MB;
    linkTypeStr_ = isLoopback ? "DLT_LINUX_SLL" : "DLT_EN10MB";
    linkOffset_ = isLoopback ? 16 : 14;

    addPacketCallBack(std::bind(
            &PFCapture::onPacket, this, _1, _2, _3));
}

PFCapture::~PFCapture()
{
    if (running_) {
        breakLoop();
    }
    logCaptureStats();
}

void PFCapture::startLoop(int packetCount)
{
    assert(!running_);
    running_ = true;

    LOG_INFO << "Capture " << name_ << ": started, link type " << linkTypeStr_;
    int err = pfring_loop(cap_, internalCallBack,
                          reinterpret_cast<u_char*>(this), packetCount);
    if (err >= 0) {
        LOG_INFO << "PFCapture: break loop";
    }
    else {
        LOG_ERROR << "PFCapture: pfring loop";
    }

    running_ = false;
}

void PFCapture::breakLoop()
{
    assert(running_);
    running_ = false;

    pfring_breakloop(cap_);
}

void PFCapture::setFilter(const char *filter)
{
    if (strlen(filter) >= 32) {
        LOG_FATAL << "filer string too long";
    }

    char data[32];
    strcpy(data, filter);
    if (pfring_set_bpf_filter(cap_, data) != 0) {
        LOG_FATAL << "set filter failed";
    }
}

void PFCapture::onPacket(const pcap_pkthdr *hdr, const u_char *data, timeval timeStamp)
{
    if (hdr->caplen <= linkOffset_) {
        LOG_WARN << "Capture: packet too short";
        return;
    }

    switch (linkType_) {

        case DLT_EN10MB:
            if (data[12] == 0x81 && data[13] == 0) {
                /* Skip 802.1Q VLAN and priority information */
                linkOffset_ = 18;
            }
            else if (data[12] != 0x08 || data[13] != 0x00) {
                LOG_DEBUG << "Capture: receive none IP packet";
                return;
            }
            break;

        case DLT_LINUX_SLL:
            if (data[14] != 0x08 || data[15] != 0x00) {
                LOG_TRACE << "Capture: receive none IP packet";
                return;
            }
            break;

        default:
            // never happened
            LOG_FATAL << "Capture: receive unsupported packet";
            return;
    }

    for (auto& func : ipFragmentCallbacks_) {
        func((ip*)(data + linkOffset_),
             hdr->caplen - linkOffset_,
             timeStamp);
    }
}

void PFCapture::logCaptureStats()
{
    pfring_stat stat;
    pfring_stats(cap_, &stat);
    LOG_INFO << "PFCapture: receive packet " << stat.recv
             << ", drop by kernel " << stat.drop;
}

void PFCapture::internalCallBack(const struct pfring_pkthdr *hdr,
                                 const u_char *data, const u_char *user)
{

    const PFCapture *cap = reinterpret_cast<const PFCapture*>(user);

    pcap_pkthdr pcapHdr;
    pcapHdr.caplen = hdr->caplen;
    pcapHdr.len = hdr->len;
    pcapHdr.ts = hdr->ts;

    for (auto& func : cap->packetCallbacks_)
    {
        func(&pcapHdr, data, hdr->ts);
    }
}