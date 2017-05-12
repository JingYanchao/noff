//
// Created by root on 17-4-17.
//

#ifndef NOFF_HTTP_H
#define NOFF_HTTP_H

#include <unordered_map>
#include <functional>
#include <string>
#include <vector>

#include <muduo/base/noncopyable.h>
#include <muduo/net/InetAddress.h>

#include "dpi/Sharding.h"
#include "dpi/TcpFragment.h"
#include "dpi/Util.h"

#include "HttpParser.h"

struct HttpRequest
{
    tuple4          t4;
    muduo::net::InetAddress srcAddr;
    muduo::net::InetAddress dstAddr;

    std::string     method;
    std::string     url;

    std::unordered_map<std::string, std::string>
                    headers;

    int bytes = 0;

    std::string     currentHeaderField;

    timeval         timeStamp;

    HttpRequest(tuple4 t4_, timeval timeStamp_):
            t4(t4_),
            srcAddr(t4.toSrcInetAddress()),
            dstAddr(t4.toDstInetAddress()),
            timeStamp(timeStamp_)
    {}
};

struct HttpResponse
{
    tuple4          t4;
    muduo::net::InetAddress srcAddr;
    muduo::net::InetAddress dstAddr;

    std::string     status;
    int             statusCode;

    std::unordered_map<std::string, std::string>
                    headers;

    int bytes = 0;

    std::string     currentHeaderField;

    timeval         timeStamp;

    HttpResponse(tuple4 t4_, timeval timeStamp_):
            t4(t4_),
            srcAddr(t4.toSrcInetAddress()),
            dstAddr(t4.toDstInetAddress()),
            timeStamp(timeStamp_)
    {}
};

std::string to_string(const HttpRequest &);
std::string to_string(const HttpResponse &);

class Http : muduo::noncopyable
{
public:

    typedef std::function<void(HttpRequest*)> HttpRequestCallback;
    typedef std::function<void(HttpResponse*)> HttpResponseCallback;

    void onTcpConnection(TcpStream*, timeval);
    void onTcpData(TcpStream*, timeval, u_char*, int, int );
    void onTcpClose(TcpStream*, timeval);
    void onTcpRst(TcpStream*, timeval);
    void onTcpTimeout(TcpStream*, timeval);

    void addHttpRequestCallback(const HttpRequestCallback& cb)
    {
        httpRequestCallbacks_.push_back(cb);
    }

    void addHttpResponseCallback(const HttpResponseCallback& cb)
    {
        httpResponseCallback_.push_back(cb);
    }

private:

    struct HttpDetail
    {
        HttpRequest     requestData;
        HttpResponse    responseData;
        HttpParser      requestParser;
        HttpParser      responseParser;

        HttpDetail(tuple4 t4, timeval timpStamp, Http *h):
                requestData(t4, timpStamp),
                responseData(t4, timpStamp)
        {
            http_parser_init(&requestParser, HTTP_REQUEST);
            http_parser_init(&responseParser, HTTP_RESPONSE);
            requestParser.data = h;
            responseParser.data = h;
        }

        bool operator == (const HttpDetail &rhs) const
        {
            return requestData.t4 == rhs.requestData.t4;
        }
    };

    typedef std::unordered_map<
            tuple4,
            HttpDetail,
            Sharding> HttpParserTable;

    HttpParserTable table;

    std::vector<HttpRequestCallback> httpRequestCallbacks_;
    std::vector<HttpResponseCallback> httpResponseCallback_;

    HttpRequest     *currentHttpRequest;
    HttpResponse    *currentHttpResponse;

    static int onHeaderFiled(HttpParser *parser, const char *data, size_t len);
    static int onHeaderValue(HttpParser *parser, const char *data, size_t len);
    static int onUrl(HttpParser *parser, const char *data, size_t len);
    static int onStatus(HttpParser *parser, const char *data, size_t len);
    static int onHeadersComplete(HttpParser *parser);

    static http_parser_settings settings;
};

#endif //NOFF_HTTP_H
