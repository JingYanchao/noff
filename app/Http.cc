//
// Created by root on 17-4-17.
//

#include <muduo/base/Logging.h>
#include "TcpFragment.h"
#include "Http.h"

int Http::onHeaderFiled(HttpParser *parser, const char *data, size_t len)
{
    Http *http = (Http*)parser->data;

    switch (parser->type) {

        case HTTP_REQUEST:
        {
            HttpRequest *request = http->currentHttpRequest;
            request->currentHeaderField.assign(data, len);
            for (char& c : request->currentHeaderField) {
                c = tolower(c);
            }
            request->bytes += len;
        }
            break;

        case HTTP_RESPONSE:
        {
            HttpResponse *response = http->currentHttpResponse;
            response->currentHeaderField.assign(data, len);
            for (char& c : response->currentHeaderField) {
                c = tolower(c);
            }
            response->bytes += len;
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

            if (headerField.empty()) {
                LOG_ERROR << "HTTP: onHeaderValue, no field before";
                return 1;
            }

            request->headers[headerField].assign(data, len);
            request->bytes += len;
        }
            break;

        case HTTP_RESPONSE:
        {
            HttpResponse *response = http->currentHttpResponse;
            std::string &headerField = response->currentHeaderField;

            if (headerField.empty()) {
                LOG_ERROR << "HTTP: onHeaderValue, no field before";
                return 1;
            }

            response->headers[headerField].assign(data, len);
            response->bytes += len;
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
    request->bytes += len;

    return 0;
}

int Http::onStatus(HttpParser *parser, const char *data, size_t len)
{
    Http *http = (Http*)parser->data;
    HttpResponse *response = http->currentHttpResponse;

    assert(parser->type == HTTP_RESPONSE);

    response->statusCode = parser->status_code;
    response->status.assign(data, len);
    response->bytes += len;

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
            request->bytes = 0;
        }
            break;

        case HTTP_RESPONSE:
        {
            HttpResponse *response = http->currentHttpResponse;

            for (auto &func : http->httpResponseCallback_) {
                func(response);
            }
            response->headers.clear();
            response->bytes = 0;
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

void Http::onTcpConnection(TcpStream *stream, timeval timeStamp)
{
    assert(stream != NULL);

    tuple4 t4 = stream->addr;
    if (t4.dest != 80) {
        return;
    }

    assert(table.find(t4) == table.end());

    HttpDetail detail(t4, timeStamp, this);

    // FIXME: table.emplace() is better?
    table.insert({t4, std::move(detail)});
}

void Http::onTcpData(TcpStream *stream, timeval timeStamp, u_char *data, int len, int flag)
{
    assert(stream != NULL);

    tuple4 t4 = stream->addr;
    if (t4.dest != 80) {
        return;
    }

    auto it = table.find(t4);

    if (it == table.end()) {
        LOG_ERROR << "HTTP: TCP data without connection";
        return;
    }

    assert(it != table.end());

    HttpDetail& detail = it->second;

    currentHttpRequest = &detail.requestData;
    currentHttpResponse = &detail.responseData;

    // parser request
    if (flag == FROMCLIENT) {

        HttpParser *parser = &detail.requestParser;

        http_parser_execute(
                parser,
                &settings,
                (char*)data,
                (size_t)len
        );

        if (parser->http_errno != 0) {
            LOG_DEBUG << "HTTP request: "
                      << http_errno_name(HTTP_PARSER_ERRNO(parser));
            // table.erase(it);
        }
    }

    // parser response
    else if (flag == FROMSERVER) {

        HttpParser *parser = &detail.responseParser;

        http_parser_execute(
                &detail.responseParser,
                &settings,
                (char*)data,
                (size_t)len
        );

        if (parser->http_errno != 0) {
            LOG_DEBUG << "HTTP response: "
                     << http_errno_name(HTTP_PARSER_ERRNO(parser));
            // table.erase(it);
        }
    }
    else {
        LOG_FATAL << "Http: unknown flag";
    }
}

void Http::onTcpClose(TcpStream *stream, timeval timeStamp)
{
    assert(stream != NULL);

    tuple4 t4 = stream->addr;
    if (t4.dest != 80) {
        return;
    }

    if (table.erase(t4) != 1) {
        LOG_ERROR << "HTTP: TCP close without connection";
    }
}

void Http::onTcpRst(TcpStream *stream, timeval timeStamp)
{
    assert(stream != NULL);

    tuple4 t4 = stream->addr;
    if (t4.dest != 80) {
        return;
    }
    table.erase(t4);
}

void Http::onTcpTimeout(TcpStream *stream, timeval timeStamp)
{
    assert(stream != NULL);

    tuple4 t4 = stream->addr;
    if (t4.dest != 80) {
        return;
    }

    if (table.erase(t4) != 1) {
        LOG_ERROR << "HTTP: TCP timeout without connection";
    }
}