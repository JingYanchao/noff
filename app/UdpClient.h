//
// Created by root on 17-5-6.
//

#ifndef NOFF_UDPCLIENT_H
#define NOFF_UDPCLIENT_H

#include <arpa/inet.h>

#include <muduo/base/noncopyable.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/InetAddress.h>

#include "Http.h"
#include "ProtocolPacketCounter.h"

class UdpClient : muduo::noncopyable
{
public:
    UdpClient(const muduo::net::InetAddress& srvaddr);
    ~UdpClient()
    {
        close(sockfd_);
    }

    void bind(const muduo::net::InetAddress& cliaddr);

    void onString(const muduo::StringPiece& str);

    void onHttpRequest(HttpRequest *rqst);
    void onHttpResponse(HttpResponse *rsps);

    void onPacketCounter(const CounterDetail&);

    template <typename T>
    void onAny(const T &data)
    {
        muduo::net::Buffer buffer;
        buffer.append(std::to_string(data));
        buffer.append("\n");
        onByteStream(buffer.peek(), buffer.readableBytes());
    }

private:
    void onByteStream(const char *data, size_t len);

    muduo::net::InetAddress srvaddr_;
    int sockfd_;
};

#endif //NOFF_UDPCLIENT_H
