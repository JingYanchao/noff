//
// Created by root on 17-5-6.
//

#include <sys/socket.h>

#include <muduo/net/Buffer.h>

#include "UdpClient.h"



using namespace muduo;
using namespace muduo::net;

namespace
{

template <typename T>
void onHttpCommon(Buffer &buffer, T *data)
{
    // timestamp
    buffer.append(std::to_string(data->timeStamp.tv_sec));

    // bytes
    buffer.append("\t");
    buffer.append(std::to_string(data->bytes));

    // src IP:Port
    buffer.append("\t");
    buffer.append(data->srcAddr.toIp());
    buffer.append("\t");
    buffer.append(std::to_string(data->srcAddr.toPort()));

    // dst IP:Port
    buffer.append("\t");
    buffer.append(data->dstAddr.toIp());
    buffer.append("\t");
    buffer.append(std::to_string(data->dstAddr.toPort()));
}

}

UdpClient::UdpClient(const InetAddress& srvaddr)
        : srvaddr_(srvaddr)
{
    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ == -1) {
        LOG_SYSFATAL << "socket error";
    }
}

void UdpClient::bind(const muduo::net::InetAddress& cliaddr)
{
    if (::bind(sockfd_, cliaddr.getSockAddr(), sizeof(sockaddr_in)) == -1) {
        LOG_SYSERR << "bind error";
    }
}

void UdpClient::onString(const StringPiece &str)
{
    onByteStream(str.data(), (size_t) str.size());
}

void UdpClient::onHttpRequest(HttpRequest *rqst)
{
    Buffer buffer;

    // common data
    onHttpCommon(buffer, rqst);

    // method
    buffer.append("\t");
    buffer.append(rqst->method);

    // host
    buffer.append("\t");
    buffer.append(rqst->headers["host"]);

    // ref
    buffer.append("\t");
    buffer.append(rqst->headers["referer"]);

    // agent
    buffer.append("\t");
    buffer.append(rqst->headers["user-agent"]);

    buffer.append("\n");

    onByteStream(buffer.peek(), buffer.readableBytes());
}

void UdpClient::onHttpResponse(HttpResponse *rsps)
{
    Buffer buffer;

    onHttpCommon(buffer, rsps);

    // status code
    buffer.append("\t");
    buffer.append(std::to_string(rsps->statusCode));

    // content type
    buffer.append("\t");
    buffer.append(rsps->headers["content-type"]);

    buffer.append("\n");

    onByteStream(buffer.peek(), buffer.readableBytes());
}

void UdpClient::onByteStream(const char *data, size_t len)
{
    assert(data != NULL);

    if (sendto(sockfd_, data, len, 0,
               srvaddr_.getSockAddr(), sizeof(sockaddr_in)) == -1) {
        LOG_SYSERR << "sendto error";
    }
}