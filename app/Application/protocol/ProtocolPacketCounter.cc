//
// Created by root on 17-5-8.
//
#include "ProtocolPacketCounter.h"

using std::string;

string to_string(const CounterDetail &d)
{
    using std::to_string;

    string buffer;
    int total = 0;

    for (int x : d) {
        buffer.append(std::to_string(x));
        buffer.append("\t");
        total += x;
    }

    buffer.append(std::to_string(total));

    return buffer;
}

void ProtocolPacketCounter::onTcpData(TcpStream *stream, timeval timeStamp)
{
    switch (stream->addr.dest)
    {
        case DNS_PORT:
            counter[0].add(1);
            break;
        case SMTP_PORT:
            counter[1].add(1);
            break;
        case POP3_PORT:
            counter[2].add(1);
            break;
        case HTTP_PORT:
            counter[3].add(1);
        case HTTPS_PORT:
            counter[4].add(1);
            break;
        case TELNET_PORT:
            counter[5].add(1);
            break;
        case FTP_PORT:
            counter[6].add(1);
            break;
        default:
            return;
    }

    bool expire = false;
    {
        muduo::MutexLockGuard guard(lock);
        if (timeStamp.tv_sec >= sendTimestamp) {
            sendTimestamp = timeStamp.tv_sec + interval_;
            expire = true;
        }
    }

    if (expire && counterCallback_) {
        counterCallback_(
                { counter[0].getAndSet(0), counter[1].getAndSet(0),
                  counter[2].getAndSet(0), counter[3].getAndSet(0),
                  counter[4].getAndSet(0), counter[5].getAndSet(0),
                  counter[6].getAndSet(0) });
    }
}