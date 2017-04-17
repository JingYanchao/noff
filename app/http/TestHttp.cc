//
// Created by root on 17-4-17.
//

#include "Http.h"

char request[] =
        "POST /joyent/http-parser HTTP/1.1\r\n"
                "Host: github.com\r\n"
                "DNT: 1\r\n"
                "Accept-Encoding: gzip, deflate, sdch\r\n"
                "Accept-Language: ru-RU,ru;q=0.8,en-US;q=0.6,en;q=0.4\r\n"
                "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10_1) "
                "AppleWebKit/537.36 (KHTML, like Gecko) "
                "Chrome/39.0.2171.65 Safari/537.36\r\n"
                "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,"
                "image/webp,*/*;q=0.8\r\n"
                "Referer: https://github.com/joyent/http-parser\r\n"
                "Connection: keep-alive\r\n"
                "Transfer-Encoding: chunked\r\n"
                "Cache-Control: max-age=0\r\n\r\nb\r\nhello world\r\n0\r\n\r\n"

                "POST /fuck/http-parser HTTP/1.1\r\n"
                "Host: github.com\r\n"
                "DNT: 1\r\n"
                "Accept-Encoding: gzip, deflate, sdch\r\n"
                "Accept-Language: ru-RU,ru;q=0.8,en-US;q=0.6,en;q=0.4\r\n"
                "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10_1) "
                "AppleWebKit/537.36 (KHTML, like Gecko) "
                "Chrome/39.0.2171.65 Safari/537.36\r\n"
                "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,"
                "image/webp,*/*;q=0.8\r\n"
                "Referer: https://github.com/joyent/http-parser\r\n"
                "Connection: keep-alive\r\n"
                "Transfer-Encoding: chunked\r\n"
                "Cache-Control: max-age=0\r\n\r\nb\r\nhello world\r\n0\r\n\r\n";

int main()
{
    Http http;

    http.addHttpRequestCallback([](const HttpRequest *rqst)
                                {
                                    return;
                                });

    tcpStream stream1;
    tcpStream stream2;

    stream1.addr = tuple4(1, 80, 2, 3);
    stream1.server.data = request;
    stream1.server.count = sizeof(request) - 1;

    stream2.addr = tuple4(4, 80, 5, 6);
    stream2.server.data = request;
    stream2.server.count = sizeof(request) - 1;

    http.onTcpConnection(&stream1, {1,2});
    http.onTcpConnection(&stream2, {3,4});

    http.onTcpData(&stream1, {3,4});
    http.onTcpData(&stream2, {3,4});

    http.onTcpClose(&stream1, {5,6});
    http.onTcpClose(&stream2, {5,6});

    return 0;
}