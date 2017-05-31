//
// Created by root on 17-5-25.
//

#include <array>
#include <boost/any.hpp>

#include "TcpSession.h"

enum {
    upFlowSize = 0,
    upPacketNum,
    upSyn, upFin, upPsh, upRst,
    upSmallPacketNum,

    downFlowSize,
    downPacketNum,
    downSyn, downFin, downPsh, downRst,
    downSmallPacketNum,
};

struct SessionData
{
    tuple4  t4_;
    timeval startTime_;
    timeval endTime_;

    std::array<int, 14> info = {};

    bool normallyClosed_ = true;

    boost::any context_;

    SessionData(tuple4 t4, timeval timeStamp) :
            t4_(t4), startTime_(timeStamp)
    {
    }
};

std::string to_string(const SessionData &data)
{
    using std::string;
    using std::to_string;

    string ret = to_string(data.t4_);
    for (int c : data.info)
    {
        ret.append("\t");
        ret.append(std::to_string(c));
    }
    ret.append("\t");
    ret.append(to_string(data.endTime_.tv_sec - data.startTime_.tv_sec));
    ret.append("\t");
    ret.append(to_string(data.normallyClosed_));

    return ret;
}

void TcpSession::onTcpData(ip *iphdr, int len, timeval timeStamp)
{
    len -= 4 * iphdr->ip_hl;
    if (len < (int)sizeof(tcphdr)) {
        return;
    }

    tcphdr *tcp = (tcphdr *)((u_char*)iphdr + 4 * iphdr->ip_hl);

    tuple4 t4(htons(tcp->source),
              htons(tcp->dest),
              iphdr->ip_src.s_addr,
              iphdr->ip_dst.s_addr);

    /* check timeout */
    if (timer_.checkTime(timeStamp)) {
        onTimer();
    }

    // printf("%lu\n", sessionDataMap_.size());

    u_int8_t flag = tcp->th_flags;
    auto it = sessionDataMap_.find(t4);

    if (it == sessionDataMap_.end()) {
        /* first connection */
        if (flag & TH_SYN) {
            auto p = sessionDataMap_.insert({t4, std::make_shared<SessionData>(t4, timeStamp)});
            onConnection(t4, p.first->second);
            updateSession(t4, p.first->second, len, flag);
        }
        return;
    }

    SessionDataPtr& ptr = it->second;
    EntryPtr entry = getEntryPtr(ptr);

    /* dead connection */
    if (entry == NULL) {
        /* another connection */
        if (flag & TH_SYN) {
            *ptr = SessionData(t4, timeStamp);
            onConnection(t4, ptr);
            updateSession(t4, ptr, len, flag);
        }
        else {
            /* keep size small */
            sessionDataMap_.erase(it);
        }
        return;
    }

    /* close connection */
    if ((flag & TH_FIN) || (flag & TH_RST)) {
        ptr->endTime_ = timeStamp;
        updateSession(t4, ptr, len, flag);
        for (auto &cb : callbacks_) {
            cb(*ptr);
        }
        sessionDataMap_.erase(it);
        return;
    }

    /* message, may have ACK+SYN */
    sessionBuckets_.back().insert(entry);
    updateSession(t4, ptr, len, flag);
}

void TcpSession::onTimer()
{
    sessionBuckets_.push_back(Bucket());
}

void TcpSession::onConnection(const tuple4& t4, SessionDataPtr& dataPtr)
{
    EntryPtr entry(new Entry(dataPtr, *this));
    WeakEntryPtr weak(entry);

    sessionBuckets_.back().insert(std::move(entry));
    dataPtr->context_ = std::move(weak);
}

void TcpSession::onTimeOut(const SessionDataPtr& dataPtr)
{
    dataPtr->normallyClosed_ = false;
    dataPtr->endTime_ = timer_.lastCheckTime();
    for (auto &cb : callbacks_) {
        cb(*dataPtr);
    }

    sessionDataMap_.erase(dataPtr->t4_);
}

void TcpSession::updateSession(const tuple4& t4, SessionDataPtr& dataPtr, int len, u_int8_t flag)
{
    /* up */
    if (t4 == dataPtr->t4_)
    {
        dataPtr->info[upFlowSize] += len;
        dataPtr->info[upPacketNum] += 1;
        if (flag & TH_SYN) ++dataPtr->info[upSyn];
        if (flag & TH_FIN) ++dataPtr->info[upFin];
        if (flag & TH_PUSH) ++dataPtr->info[upPsh];
        if (flag & TH_RST) ++dataPtr->info[upRst];
        dataPtr->info[upSmallPacketNum] += (len <= 64);
    }
        /* down */
    else {
        dataPtr->info[downFlowSize] += len;
        dataPtr->info[downPacketNum] += 1;
        if (flag & TH_SYN) ++dataPtr->info[downSyn];
        if (flag & TH_FIN) ++dataPtr->info[downFin];
        if (flag & TH_PUSH) ++dataPtr->info[downPsh];
        if (flag & TH_RST) ++dataPtr->info[downRst];
        dataPtr->info[downSmallPacketNum] += (len <= 64);
    }
}

TcpSession::Entry::~Entry()
{
    SessionDataPtr ptr = weak_.lock();
    if (ptr != NULL) {
        tcpSession_.onTimeOut(ptr);
    }
}

TcpSession::EntryPtr TcpSession::getEntryPtr(SessionDataPtr &dataPtr)
{
    assert(!dataPtr->context_.empty());
    WeakEntryPtr weakEntry(boost::any_cast<WeakEntryPtr>(dataPtr->context_));
    return weakEntry.lock();
}