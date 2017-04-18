//
// Created by jyc on 17-4-16.
//
#include "../dpi/Capture.h"
#include "../dpi/Dispatcher.h"
#include "../dpi/IpFragment.h"

#include <signal.h>

#include <muduo/base/Atomic.h>
#include <muduo/base/Logging.h>
class protocol
{
public:
    void tcp_output(ip* data,int len,timeval timeStamp)
    {
        tcp_num.add(1);
    }

    void udp_output(ip* data,int len,timeval timeStamp)
    {
        udp_num.add(1);
    }

    void icmp_output(ip* data,int len,timeval timeStamp)
    {
        icmp_num.add(1);
    }
    ~protocol()
    {
        LOG_INFO<<"tcp:"<<tcp_num.get()<<" "<<"udp:"<<udp_num.get()<<" "<<"icmp:"<<icmp_num.get();
    }


private:
    muduo::AtomicInt32 tcp_num;
    muduo::AtomicInt32 udp_num;
    muduo::AtomicInt32 icmp_num;

};
Capture cap("eno2", 65536, true, 1000);
protocol ptc;
void sigHandler(int)
{
    cap.breakLoop();
}


int main()
{
    muduo::Logger::setLogLevel(muduo::Logger::DEBUG);

    signal(SIGINT, sigHandler);

    cap.setFilter("ip");

    // customized function, count IP fragments
    IpFragment frag[1];

    // connect Dispatcher and IpFragmentCounter
    size_t nWorkers = 1;
    std::vector<Dispatcher::IpFragmentCallback> callbacks;
    for(int i=0;i<1;i++)
    {
        frag[i].addTcpCallback(std::bind(&protocol::tcp_output,&ptc,std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));
        frag[i].addUdpCallback(std::bind(&protocol::udp_output,&ptc,std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));
        frag[i].addIcmpCallback(std::bind(&protocol::icmp_output,&ptc,std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));
        callbacks.push_back(std::bind(&IpFragment::startIpfragProc, &frag[i], std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));
    }

    // if queue is full, Dispatcher will warn
    // Dispatcher dis(callbacks, 2);
    Dispatcher dispatcher(callbacks, 65536);

    // connect Capture and Dispatcher
    cap.addIpFragmentCallback(std::bind(
            &Dispatcher::onIpFragment, &dispatcher,std::placeholders:: _1, std::placeholders::_2,std::placeholders::_3));

    cap.startLoop(0);
}

