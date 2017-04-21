//
// Created by root on 17-4-17.
//

#include <unordered_map>
#include <functional>
#include <string>
#include <vector>

#include <muduo/base/noncopyable.h>

#include "dpi/Sharding.h"
#include "dpi/TcpFragment.h"
#include "dpi/Util.h"

#include "HttpParser.h"

#ifndef NOFF_HTTP_H
#define NOFF_HTTP_H

struct HttpRequest
{
    tuple4          t4;
    std::string     method;
    std::string     url;

    std::unordered_map<std::string, std::string>
                    headers;

    std::string     currentHeaderField;

    timeval         timeStamp;

    HttpRequest(tuple4 t4_, timeval timeStamp_):
            t4(t4_), timeStamp(timeStamp_)
    {}
};

struct HttpResponse
{
    tuple4          t4;
    std::string     statusCode;

    std::unordered_map<std::string, std::string>
                    headers;

    std::string     currentHeaderField;

    timeval         timeStamp;

    HttpResponse(tuple4 t4_, timeval timeStamp_):
            t4(t4_), timeStamp(timeStamp_)
    {}
};

class Http : muduo::noncopyable
{
public:

    typedef std::function<void(const HttpRequest*)> HttpRequestCallback;
    typedef std::function<void(const HttpResponse*)> HttpResponseCallback;

    void onTcpConnection(tcpStream*, timeval);
    void onTcpData(tcpStream*, timeval);
    void onTcpClose(tcpStream*, timeval);
    void onTcpRst(tcpStream*, timeval);
    void onTcpTimeout(tcpStream*, timeval);

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
        HttpParser     requestParser;
        HttpParser     responseParser;

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
