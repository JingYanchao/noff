//
// Created by frabk on 17-4-10.
//

#include <assert.h>
#include <stdio.h>
#include <string>
#include <muduo/base/Logging.h>

#include "Capture.h"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

Capture::Capture(const char *deviceName, int snaplen, bool promisc, int msTimeout)
        :name_(deviceName),
         isLive(true)
{
    pcap_ = pcap_open_live(deviceName, snaplen, promisc, msTimeout, errBuf_);

    // may warning
    if (pcap_ != NULL && errBuf_[0] != '\0') {
        LOG_WARN << "Capture " << errBuf_;
    }
    if (pcap_ == NULL) {
        LOG_ERROR << "Capture " << errBuf_;
        exit(1);
    }

    initLinkType();

    LOG_TRACE << "Capture: open, link type " << linkTypeStr_;

    addPacketCallBack(std::bind(
            &Capture::onPacket, this, _1, _2, _3));
}

Capture::Capture(const char *fileName)
        :name_(fileName),
         isLive(false)
{
    pcap_ = pcap_open_offline(fileName, errBuf_);
    if (pcap_ == NULL) {
        LOG_ERROR << "Capture " << errBuf_;
        exit(1);
    }

    initLinkType();

    LOG_DEBUG << "Capture: open, link type " << linkTypeStr_;

    addPacketCallBack(std::bind(
            &Capture::onPacket, this, _1, _2, _3));
}

Capture::~Capture()
{
    if (running_) {
        breakLoop();
    }

    pcap_close(pcap_);

    LOG_INFO << "Capture: stopped";
}

void Capture::startLoop(int packetCount)
{
    assert(!running_);
    running_ = true;

    LOG_INFO << "Capture " << name_ << ": started, link type " << linkTypeStr_;

    int err = pcap_loop(pcap_, packetCount, internalCallBack, reinterpret_cast<u_char*>(this));
    switch (err) {
        case 0:
            LOG_INFO << "Capture: break loop on "
                     << packetCount << " packet";
            break;
        case -1:
            LOG_ERROR << "Capture " << pcap_geterr(pcap_);
            break;
        case -2:
            LOG_INFO << "Capture: break loop by user";
            break;
        default:
            LOG_FATAL << "Capture: break loop, unknown error(" << err << ")";
            break;
    }

    logCaptureStats();

    running_ = false;
}

void Capture::breakLoop()
{
    assert(running_);
    running_ = false;

    pcap_breakloop(pcap_);
}

void Capture::setFilter(const char *filter)
{
    if (pcap_compile(pcap_, &filter_, filter, 1, 0) < 0)
    {
        LOG_ERROR << "pcap compile: " << pcap_geterr(pcap_);
        exit(1);
    }

    if (pcap_setfilter(pcap_, &filter_) == -1)
    {
        LOG_ERROR << "pcap setfilter: " << pcap_geterr(pcap_);
        exit(1);
    }
}

void Capture::logCaptureStats()
{
    if (!isLive) {
        return;
    }

    pcap_stat stat;
    pcap_stats(pcap_, &stat);
    LOG_INFO << "Capture: receive packet " << stat.ps_recv
             << ", drop by kernel " << stat.ps_drop
             << ", drop by filter " << stat.ps_ifdrop;
}

void Capture::initLinkType()
{
    assert(pcap_ != NULL);

    linkType_ = pcap_datalink(pcap_);
    linkTypeStr_ = pcap_datalink_val_to_name(linkType_);

    assert(linkTypeStr_ != NULL);

    switch (linkType_) {
        // ethernet packet
        case DLT_EN10MB:
            linkOffset_ = 14;
            break;
        case DLT_LINUX_SLL:
            linkOffset_ = 16;
            break;
        case PCAP_ERROR_NOT_ACTIVATED:
            // can never happen
            LOG_FATAL << "Capture: not activated";
            break;
        default:
            LOG_ERROR << "capture " << name_
                      <<": unsupported link type "
                      << linkTypeStr_;
            exit(1);
    }
}

void Capture::onPacket(const pcap_pkthdr *hdr, const u_char *data, timeval timeStamp)
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

void Capture::internalCallBack(u_char *user, const struct pcap_pkthdr *hdr, const u_char *data)
{

    Capture *cap = reinterpret_cast<Capture*>(user);

    for (auto& func : cap->packetCallbacks_)
    {
        func(hdr, data, hdr->ts);
    }
}