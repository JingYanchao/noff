//
// Created by jyc on 17-4-16.
//
#include "Capture.h"
#include "Dispatcher.h"
#include "Ip_fragment.h"

#include <signal.h>

#include <muduo/base/Atomic.h>
#include <muduo/base/Logging.h>

using std::placeholders::_1;
using std::placeholders::_2;

class TcpOutput {

public:
    void tcp_output(u_char *data, int len)
    {
        counter.add(1);
    }

    ~TcpOutput()
    {
        LOG_INFO << "tcp output = " << counter.get();
    }

private:
    muduo::AtomicInt32 counter;
};

class UdpOutput {

public:
    void udp_output(char *data)
    {
        counter.add(1);
    }

    ~UdpOutput()
    {
        LOG_INFO << "udp output = " << counter.get();
    }

private:
    muduo::AtomicInt32 counter;
};

class IcmpOutput
{
public:
    void icmp_output(u_char *data)
    {
        counter.add(1);
    }

    ~IcmpOutput()
    {
        LOG_INFO << "icmp output = " << counter.get();
    }

private:
    muduo::AtomicInt32 counter;
};

Capture cap("eno2", 65536, true, 1000);

void sigHandler(int)
{
    cap.breakLoop();
}

int main()
{
    muduo::Logger::setLogLevel(muduo::Logger::TRACE);

    signal(SIGINT, sigHandler);

    cap.setFilter("ip");

    size_t nWorkers = 1;

    // customized function, count IP fragments
    TcpOutput tcpOutput;
    UdpOutput udpOutput;
    IcmpOutput icmpOutput;
    Ip_fragment frag[nWorkers];

    // connect Dispatcher and IpFragmentCounter
    std::vector<Dispatcher::IpFragmentCallback> callbacks;
    for(size_t i=0;i < nWorkers;i++)
    {
        frag[i].addTcpCallback(std::bind(&TcpOutput::tcp_output, &tcpOutput,  _1, _2));
        frag[i].addUdpCallback(std::bind(&UdpOutput::udp_output, &udpOutput, _1));
        frag[i].addIcmpCallback(std::bind(&IcmpOutput::icmp_output, &icmpOutput, _1));
        callbacks.push_back(std::bind(&Ip_fragment::start_ip_frag_proc, &frag[i], _1, _2));
    }

    // if queue is full, Dispatcher will warn
    // Dispatcher dis(callbacks, 2);
    Dispatcher dispatcher(callbacks, 65536);

    // connect Capture and Dispatcher
    cap.addIpFragmentCallback(std::bind(
            &Dispatcher::onIpFragment, &dispatcher,std::placeholders:: _1, std::placeholders::_2));

    cap.startLoop(0);
}

