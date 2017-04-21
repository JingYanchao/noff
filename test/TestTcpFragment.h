//
// Created by jyc on 17-4-20.
//

#ifndef TCPFRAGMENT_TEST_TCPFRAGMENT_H
#define TCPFRAGMENT_TEST_TCPFRAGMENT_H
#include "../dpi/TcpFragment.h"
#include <signal.h>

#include <muduo/base/Atomic.h>
#include <muduo/base/Logging.h>
void tcpConnection(TcpStream* tcp_connection,timeval timeStamp)
{
//    LOG_INFO<<"connect";
    return ;
}

void tcpData(TcpStream* tcp_connection,timeval timeStamp,u_char* u_data,int datalen)
{
//    LOG_INFO<<"data";
    u_data[datalen-1]='\0';
    LOG_INFO<<"data:"<<u_data;
    return;
}

void tcpClose(TcpStream* tcp_connection,timeval timeStamp)
{
    LOG_INFO<<"close";
    return;
}

void tcpRst(TcpStream* tcp_connection,timeval timeStamp)
{
//    LOG_INFO<<"rst";
    return;
}

void tcpTimeOut(TcpStream* tcp_connection,timeval timeStamp)
{
    LOG_WARN<<"timeout";
    return ;
}



#endif //TCPFRAGMENT_TEST_TCPFRAGMENT_H
