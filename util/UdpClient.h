//
// Created by root on 17-5-6.
//

#ifndef NOFF_UDPCLIENT_H
#define NOFF_UDPCLIENT_H

#include <arpa/inet.h>

#include <muduo/base/noncopyable.h>
#include <muduo/base/Logging.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/InetAddress.h>

class UdpClient : muduo::noncopyable
{
public:
    UdpClient(const muduo::net::InetAddress& srvaddr, const std::string& name = "debug");
    ~UdpClient()
    {
        close(sockfd_);
    }

    void bind(const muduo::net::InetAddress& cliaddr);

    void onString(const std::string& str)
    {
        onByteStream(str.data(), str.size());
    }

    template <typename T>
    void onData(const T &data)
    {
        using namespace std; // ??? wtf

        std::string buffer = to_string(data);
        buffer.append("\n");

        onByteStream(buffer.data(), buffer.length());
    }

    template <typename T>
    void onDataPointer(const T *data)
    {
        onData(*data);
    }

private:
    void onByteStream(const char *data, size_t len);

    muduo::net::InetAddress srvaddr_;
    int sockfd_;
};

#endif //NOFF_UDPCLIENT_H
