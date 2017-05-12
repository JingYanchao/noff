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

        case SMTP_PORT:
            counter[1].increment();
            break;
        case POP3_PORT:
            counter[2].increment();
            break;
        case HTTP_PORT:
            counter[3].increment();
        case HTTPS_PORT:
            counter[4].increment();
            break;
        case TELNET_PORT:
            counter[5].increment();
            break;
        case FTP_PORT:
            counter[6].increment();
            break;
        default:
            return;
    }

    if (timer.checkTime(timeStamp) && counterCallback_) {
        counterCallback_(
                { counter[0].getAndSet(0), counter[1].getAndSet(0),
                  counter[2].getAndSet(0), counter[3].getAndSet(0),
                  counter[4].getAndSet(0), counter[5].getAndSet(0),
                  counter[6].getAndSet(0) });
    }
}

void ProtocolPacketCounter::onUdpData(tuple4 t4, char *, int, timeval timeStamp)
{
    if (t4.dest == DNS_PORT || t4.source == DNS_PORT) {
        counter[0].increment();
        if (timer.checkTime(timeStamp) && counterCallback_) {
            counterCallback_(
                    { counter[0].getAndSet(0), counter[1].getAndSet(0),
                      counter[2].getAndSet(0), counter[3].getAndSet(0),
                      counter[4].getAndSet(0), counter[5].getAndSet(0),
                      counter[6].getAndSet(0) });
        }
    }
}