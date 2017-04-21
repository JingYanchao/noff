//
// Created by jyc on 17-4-17.
//

#ifndef NOFF_TEST_TCPFRAGMENT_H
#define NOFF_TEST_TCPFRAGMENT_H

#include "../dpi/TcpFragment.h"
#include <signal.h>

#include <muduo/base/Atomic.h>
#include <muduo/base/Logging.h>

void tcpConnection(tcpStream* tcp_connection,timeval timeStamp)
{
    LOG_TRACE<<"connect";
    if(tcp_connection->addr.dest==80 )
    {
        LOG_TRACE<<"the tcp state is: "<<tcp_connection->nids_state;
        return ;
    }
}

void tcpData(tcpStream* tcp_connection,timeval timeStamp)
{
    LOG_TRACE<<"data";
    if(tcp_connection->addr.dest==80 )
    {
        LOG_TRACE<<"the tcp state is: "<<tcp_connection->nids_state;
        return ;
    }
}

void tcpClose(tcpStream* tcp_connection,timeval timeStamp)
{
    LOG_TRACE<<"close";
    if(tcp_connection->addr.dest==80 )
    {
        LOG_TRACE<<"the tcp state is: "<<tcp_connection->nids_state;
        return ;
    }
}

void tcpRst(tcpStream* tcp_connection,timeval timeStamp)
{
    LOG_TRACE<<"rst";
    if(tcp_connection->addr.dest==80 )
    {
        LOG_TRACE<<"the tcp state is: "<<tcp_connection->nids_state;
        return ;
    }
}

void tcpTimeOut(tcpStream* tcp_connection,timeval timeStamp)
{
    LOG_INFO<<"timeout";
    if(tcp_connection->addr.dest==80 )
    {
        LOG_TRACE<<"the tcp state is:"<<tcp_connection->nids_state;
        return ;
    }
}

#endif //NOFF_TEST_TCPFRAGMENT_H
