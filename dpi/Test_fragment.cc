//
// Created by jyc on 17-4-16.
//
#include "Capture.h"
#include "Dispatcher.h"
#include "Ip_fragment.h"

#include <signal.h>

#include <muduo/base/Atomic.h>
#include <muduo/base/Logging.h>

void tcp_output(u_char* data,int len)
{
    printf("this tcp\n");
}

void udp_output(char* data)
{
    printf("this udp\n");
}

void icmp_output(u_char* data)
{
    printf("this icmp");
}

Capture cap("any", 65536, true, 1000);

void sigHandler(int)
{
    cap.breakLoop();
}

int main()
{
    muduo::Logger::setLogLevel(muduo::Logger::TRACE);

    signal(SIGINT, sigHandler);

    cap.setFilter("ip");

    // customized function, count IP fragments
    Ip_fragment frag[4];

    // connect Dispatcher and IpFragmentCounter
    size_t nWorkers = 4;
    std::vector<Dispatcher::IpFragmentCallback> callbacks;
    for(int i=0;i<4;i++)
    {
        frag[i].addTcpCallback(std::bind(&tcp_output,std::placeholders::_1, std::placeholders::_2));
        frag[i].addUdpCallback(std::bind(&udp_output,std::placeholders::_1));
        frag[i].addIcmpCallback(std::bind(&icmp_output,std::placeholders::_1));
        callbacks.push_back(std::bind(&Ip_fragment::start_ip_frag_proc, &frag[i], std::placeholders::_1, std::placeholders::_2));
    }

    // if queue is full, Dispatcher will warn
    // Dispatcher dis(callbacks, 2);
    Dispatcher dispatcher(callbacks, 1024);

    // connect Capture and Dispatcher
    cap.addIpFragmentCallback(std::bind(
            &Dispatcher::onIpFragment, &dispatcher,std::placeholders:: _1, std::placeholders::_2));

    cap.startLoop(0);
}

