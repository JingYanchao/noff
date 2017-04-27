//
// Created by root on 17-4-27.
//
//
// Created by jyc on 17-4-18.
//
#include "../dpi/Capture.h"
#include "../dpi/Dispatcher.h"
#include "../dpi/IpFragment.h"
#include "../dpi/TcpFragment.h"
#include "../dpi/Udp.h"
#include "../app/dns/Dnsparser.h"

#include <signal.h>

#include <muduo/base/Atomic.h>
#include <muduo/base/Logging.h>
const int NUM_THREAD = 1;
class protocol
{
public:
    void tcp_output(ip* data,int len,timeval timeStamp)
    {
        tcp_num.add(1);
    }

    void udp_output(ip* data,int len,timeval timeStamp)
    {
        udp_num.increment();
    }

    void icmp_output(ip* data,int len,timeval timeStamp)
    {
        icmp_num.increment();
    }
    void other_output(ip* data,int len,timeval timeStamp)
    {
        other_num.increment();
    }
    ~protocol()
    {
        LOG_INFO<<"tcp:"<<tcp_num.get()<<" "<<"udp:"<<udp_num.get()<<" "<<"icmp:"<<icmp_num.get()<<"other:"<<other_num.get();
    }


private:
    muduo::AtomicInt32 tcp_num;
    muduo::AtomicInt32 udp_num;
    muduo::AtomicInt32 icmp_num;
    muduo::AtomicInt32 other_num;

};
Capture cap("eno2",65536,true,1024);
//Capture cap("/home/jyc/data/testhttp2.pcap");
protocol ptc;
void sigHandler(int)
{
    cap.breakLoop();
}


int main()
{
    muduo::Logger::setLogLevel(muduo::Logger::INFO);

    signal(SIGINT, sigHandler);

    cap.setFilter("ip");

    // customized function, count IP fragments
    IpFragment frag[NUM_THREAD];
    Udp UdpFrag[NUM_THREAD];
    DnsParser dnsParser[NUM_THREAD];

    // connect Dispatcher and IpFragmentCounter
    std::vector<Dispatcher::IpFragmentCallback> callbacks;
    for(int i=0;i<NUM_THREAD;i++)
    {
        UdpFrag[i].addudpCallback(std::bind(&DnsParser::processDns,&dnsParser[i],std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4));

    }

    for(int i=0;i<NUM_THREAD;i++)
    {
        frag[i].addTcpCallback(std::bind(&protocol::tcp_output,&ptc,std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));
//        frag[i].addTcpCallback(std::bind(&TcpFragment::processTcp,&tcpFrag[i],std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));
        frag[i].addUdpCallback(std::bind(&Udp::processUdp,&UdpFrag[i],std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));
        frag[i].addIcmpCallback(std::bind(&protocol::icmp_output,&ptc,std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));
//        cap.addIpFragmentCallback(std::bind(&IpFragment::startIpfragProc, &frag[i], std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));
        callbacks.push_back(std::bind(&IpFragment::startIpfragProc, &frag[i], std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));
    }

    // if queue is full, Dispatcher will warn
    Dispatcher dis(callbacks, 65536);

    cap.addIpFragmentCallback(std::bind(
            &Dispatcher::onIpFragment, &dis,std::placeholders:: _1, std::placeholders::_2, std::placeholders::_3));

    cap.startLoop(0);

}

