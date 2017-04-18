//
// Created by root on 17-4-17.
//

#include <muduo/base/Logging.h>

#include "Http.h"

int Http::onHeaderFiled(HttpParser *parser, const char *data, size_t len)
{
    Http *http = (Http*)parser->data;

    switch (parser->type) {

        case HTTP_REQUEST:
        {
            HttpRequest *request = http->currentHttpRequest;
            request->currentHeaderField.assign(data, len);
        }
            break;

        case HTTP_RESPONSE:
        {
            HttpResponse *response = http->currentHttpResponse;
            response->currentHeaderField.assign(data, len);
        }
            break;

        default:
            // never reach here
            LOG_FATAL << "HTTP: unknown parser type";
    }
    return 0;
}

int Http::onHeaderValue(HttpParser *parser, const char *data, size_t len)
{
    Http *http = (Http*)parser->data;

    switch (parser->type) {

        case HTTP_REQUEST:
        {
            HttpRequest *request = http->currentHttpRequest;
            std::string &headerField = request->currentHeaderField;

            assert(!headerField.empty());

            request->headers[headerField].assign(data, len);
        }
            break;

        case HTTP_RESPONSE:
        {
            HttpResponse *response = http->currentHttpResponse;
            std::string &headerField = response->currentHeaderField;

            assert(!headerField.empty());

            response->headers[headerField].assign(data, len);
        }
            break;

        default:
            // never reach here
            LOG_FATAL << "HTTP: unknown parser type";
    }
    return 0;
}

int Http::onUrl(HttpParser *parser, const char *data, size_t len)
{
    Http *http = (Http*)parser->data;
    HttpRequest *request = http->currentHttpRequest;

    assert(parser->type == HTTP_REQUEST);

    request->url.assign(data, len);

    return 0;
}

int Http::onStatus(HttpParser *parser, const char *data, size_t len)
{
    Http *http = (Http*)parser->data;
    HttpResponse *response = http->currentHttpResponse;

    assert(parser->type == HTTP_REQUEST);

    response->statusCode.assign(data, len);

    return 0;
}

int Http::onHeadersComplete(HttpParser *parser)
{
    Http *http = (Http*)parser->data;

    switch (parser->type)
    {
        case HTTP_REQUEST:
        {
            HttpRequest *request = http->currentHttpRequest;
            request->method = http_method_str(
                    (enum http_method) parser->method);

            for (auto &func : http->httpRequestCallbacks_) {
                func(request);
            }
            request->headers.clear();
        }
            break;

        case HTTP_RESPONSE:
        {
            HttpResponse *response = http->currentHttpResponse;

            for (auto &func : http->httpResponseCallback_) {
                func(response);
            }
            response->headers.clear();
        }
            break;

        default:
            LOG_FATAL << "HTTP: unknown parser type";
    }

    return 0;
}


http_parser_settings Http::settings = {
        NULL,               // on_message_begin
        onUrl,              // on_url
        onStatus,           // on_status
        onHeaderFiled,      // on_header_field
        onHeaderValue,      // on_header_value
        onHeadersComplete,  // on_headers_complete
        NULL,               // on_body
        NULL,               // on_message_complete
        NULL,               // on_chunk_header;
        NULL,               // on_chunk_complete;
};

void Http::onTcpConnection(tcpStream *stream, timeval timeStamp)
{
    tuple4 t4 = stream->addr;
    if (t4.dest != 80) {
        return;
    }

    assert(table.find(t4) == table.end());

    HttpDetail detail(t4, timeStamp, this);

    // FIXME: table.emplace() is better?
    table.insert({t4, std::move(detail)});
}

void Http::onTcpData(tcpStream *stream, timeval timeStamp)
{
    tuple4 t4 = stream->addr;
    if (t4.dest != 80) {
        return;
    }

    auto it = table.find(t4);

    assert(it != table.end());

    HttpDetail& detail = it->second;

    currentHttpRequest = &detail.requestData;
    currentHttpResponse = &detail.responseData;

    // parser request
    if (char *data = stream->server.data) {

        HttpParser *parser = &detail.requestParser;

        http_parser_execute(
                parser,
                &settings,
                data,
                (size_t) stream->server.count
        );

        if (parser->http_errno != 0) {
            LOG_WARN << "HTTP request: "
                     << http_errno_name(HTTP_PARSER_ERRNO(parser));
        }
    }

    // parser response
    if (char *data = stream->client.data) {

        HttpParser *parser = &detail.responseParser;

        http_parser_execute(
                &detail.responseParser,
                &settings,
                data,
                (size_t) stream->client.count
        );

        if (parser->http_errno != 0) {
            LOG_WARN << "HTTP response: "
                     << http_errno_name(HTTP_PARSER_ERRNO(parser));
        }
    }
}

void Http::onTcpClose(tcpStream *stream, timeval)
{
    tuple4 t4 = stream->addr;
    if (t4.dest != 80) {
        return;
    }

    if (table.erase(t4) != 1) {
        LOG_FATAL << "HTTP: TCP close without connection";
    }
}

void Http::onTcpRst(tcpStream *stream, timeval)
{
    tuple4 t4 = stream->addr;
    if (t4.dest != 80) {
        return;
    }

    if (table.erase(t4) != 1) {
        LOG_FATAL << "HTTP: TCP reset without connection";
    }
}

void Http::onTcpTimeout(tcpStream *stream, timeval)
{
    tuple4 t4 = stream->addr;
    if (t4.dest != 80) {
        return;
    }

    if (table.erase(t4) != 1) {
        LOG_FATAL << "HTTP: TCP timeout without connection";
    }
}