//
// Created by root on 17-5-6.
//

#include <sys/socket.h>

#include <muduo/net/Buffer.h>

#include "UdpClient.h"

using namespace muduo;
using namespace muduo::net;


UdpClient::UdpClient(const InetAddress& srvaddr, const std::string& name)
        : srvaddr_(srvaddr)
{
    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ == -1) {
        LOG_SYSFATAL << "socket error";
    }

    LOG_INFO << "["<< name << "] ";
}

void UdpClient::bind(const muduo::net::InetAddress& cliaddr)
{
    if (::bind(sockfd_, cliaddr.getSockAddr(), sizeof(sockaddr_in)) == -1) {
        LOG_SYSERR << "bind error";
    }
}

void UdpClient::onByteStream(const char *data, size_t len)
{
    assert(data != NULL);

    if (sendto(sockfd_, data, len, 0,
               srvaddr_.getSockAddr(), sizeof(sockaddr_in)) == -1) {
        LOG_SYSERR << "sendto error";
    }
}