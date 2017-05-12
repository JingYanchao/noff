//
// Created by root on 17-4-26.
//

#ifndef DNSPARSER_DNSPARSER_H
#define DNSPARSER_DNSPARSER_H

#include "../../dpi/Util.h"
#include <stdlib.h>
#include <list>
#include <functional>
#include <vector>
struct DnsQuestion
{
    char * name;
    u_int16_t type;
    u_int16_t cls;
};

struct DnsRR
{
    char * name;
    u_int16_t type;
    u_int16_t cls;
    u_int16_t ttl;
    u_int16_t rdlength;
    u_int16_t dataLen;
    char * data;
};

struct DnsInfo
{
    char* name;
    u_int16_t id;
    char qr;
    char AA;
    char TC;
    u_int8_t rcode;
    u_int8_t opcode;
    u_int16_t qdcount;
    std::list<DnsQuestion> queries;
    u_int16_t ancount;
    std::list<DnsRR> answers;
    u_int16_t nscount;
    std::list<DnsRR> name_servers;
    u_int16_t arcount;
    std::list<DnsRR> additional;
};

struct DnsRequest
{
    timeval timeStamp;
    u_int32_t srcip;
    u_int32_t dstip;
    std::string Qname;
    u_int16_t Qtype;
    u_int16_t Qclass;
};

struct DnsResponse
{
    timeval timeStamp;
    u_int32_t srcip;
    u_int32_t dstip;
    std::string Rname;
    u_int16_t Rtype;
    u_int16_t Rclass;
    int ttl;
    std::string result;
};

std::string to_string(const DnsRequest&);
std::string to_string(const DnsResponse&);

class DnsParser
{
public:
    typedef std::function<void(DnsResponse&)> ResponseCallback;
    typedef std::function<void(DnsRequest&)> RequestCallback;
    void addRequstecallback(const RequestCallback& cb)
    {
        dnsrequestCallback_.push_back(cb);
    }
    void addResponsecallback(const ResponseCallback& cb)
    {
        dnsresponseCallback_.push_back(cb);
    }
    DnsParser();
    ~DnsParser();
    u_int32_t processDns(tuple4 udptuple, char* data, int datalen,timeval timeStamp);

private:
    std::vector<RequestCallback> dnsrequestCallback_;
    std::vector<ResponseCallback> dnsresponseCallback_;

    u_int32_t parserQuestions(char* data,u_int32_t pos,u_int16_t count,DnsInfo&,int);
    char* readRRname(char* data,u_int32_t* pos,u_int32_t id_pos,int datalen);
    uint32_t parseRR(char* data,uint32_t pos,DnsRR& rr,int datalen);
    uint32_t parseRRset(char *data, u_int32_t pos,u_int16_t count, DnsInfo &dnsinfo,int datalen);
    char * A(char* packet, uint32_t pos, u_int16_t rdlength);
    u_int32_t numRequest;
    u_int32_t numResponse;
    u_int32_t numUdp;

};

#endif //DNSPARSER_DNSPARSER_H
