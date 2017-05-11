//
// Created by root on 17-4-26.
//
#include "Dnsparser.h"
#include <muduo/base/Logging.h>

DnsParser::DnsParser()
{
    numRequest = 0;
    numResponse = 0;
    numUdp = 0;
}

DnsParser::~DnsParser()
{
    LOG_INFO<<"Udp:"<<numUdp;
    LOG_INFO<<"DnsRequest:"<<numRequest<<" DnsResponse:"<<numResponse;
}

u_int32_t DnsParser::processDns(tuple4 udptuple, char *data, int datalen, timeval timeStamp)
{
    //dns response
    u_int32_t pos = 0;
    numUdp++;
    if(udptuple.source == 53)
    {
        DnsResponse dnsResponse;
        if(datalen<12)
        {
            LOG_INFO<<"this is invalied";
            return 0;
        }
        DnsInfo dns;
        char* this_dns = data;
        dns.id = this_dns[pos]<<8+this_dns[pos+1];
        dns.qr = this_dns[pos+2] >> 7;
        dns.AA = (this_dns[pos+2]&0x04)>>2;
        dns.TC = (this_dns[pos+2]&0x02)>>1;
        dns.rcode =this_dns[pos+3]&0x0f;
        if(dns.rcode>5)
        {
            dns.qdcount = dns.ancount = dns.nscount = dns.arcount = 0;
            dns.queries.clear();
            dns.answers.clear();
            return pos+12;
        }
        dns.qdcount =(this_dns[pos+4] << 8) +this_dns[pos+5];
        dns.ancount = (this_dns[pos+6] << 8) +this_dns[pos+7];
        dns.nscount = (this_dns[pos+8] << 8) +this_dns[pos+9];
        dns.arcount = (this_dns[pos+10] << 8) +this_dns[pos+11];
        pos = parserQuestions(this_dns,pos+12,dns.qdcount,dns,datalen);
        if (pos != 0)
            pos = parseRRset(this_dns,pos,dns.ancount,dns,datalen);
        else
            dns.answers.clear();
        numResponse++;
        char temp_ip_address[30];

        for(auto& res:dns.answers)
        {
            dnsResponse.timeStamp = timeStamp;
            dnsResponse.srcip = udptuple.saddr;
            dnsResponse.dstip = udptuple.daddr;
            dnsResponse.Rname.assign(res.name,strlen(res.name));
            dnsResponse.Rclass = res.cls;
            dnsResponse.Rtype = res.type;
            dnsResponse.ttl = res.ttl;
            dnsResponse.result.assign(res.data,strlen(res.data));
        }

        for(auto& func:dnsresponseCallback_)
        {
            if(!dnsResponse.Rname.empty())
                func(dnsResponse);
        }

        for(auto& res:dns.queries)
            free(res.name);
        for(auto& res:dns.answers)
            free(res.data);
        return pos;
    }
    else if(udptuple.dest == 53) //dns requeset
    {
        DnsRequest dnsRequest;
        if(datalen<12)
        {
            LOG_INFO<<"this is invalied";
            return 0;
        }
        DnsInfo dns;
        char* this_dns = data;
        dns.id = this_dns[pos]<<8+this_dns[pos+1];
        dns.qr = this_dns[pos+2] >> 7;
        dns.AA = (this_dns[pos+2]&0x04)>>2;
        dns.TC = (this_dns[pos+2]&0x02)>>1;
        dns.rcode =this_dns[pos+3]&0x0f;
        if(dns.rcode>5)
        {
            dns.qdcount = dns.ancount = dns.nscount = dns.arcount = 0;
            dns.queries.clear();
            dns.answers.clear();
            return pos+12;
        }
        dns.qdcount =(this_dns[pos+4] << 8) +this_dns[pos+5];
        dns.ancount = (this_dns[pos+6] << 8) +this_dns[pos+7];
        dns.nscount = (this_dns[pos+8] << 8) +this_dns[pos+9];
        dns.arcount = (this_dns[pos+10] << 8) +this_dns[pos+11];
        if(dns.qdcount!=1&&dns.ancount!=0&&dns.nscount!=0&&dns.arcount!=0)
            LOG_INFO<<"this is not request dns packet";
        pos = parserQuestions(this_dns,pos+12,dns.qdcount,dns,datalen);
        numRequest++;
        for(auto& res:dns.queries)
        {
            dnsRequest.Qname.assign(res.name,strlen(res.name));
            dnsRequest.Qtype = res.type;
            dnsRequest.Qtype = res.cls;
            dnsRequest.srcip = udptuple.saddr;
            dnsRequest.dstip = udptuple.daddr;
            dnsRequest.timeStamp = timeStamp;
        }
//            LOG_INFO<<"the dns request is:"<<res.name<<"type:"<<res.type;

        for(auto& func:dnsrequestCallback_)
        {
            func(dnsRequest);
        }

        for(auto& res:dns.queries)
            free(res.name);
    }
    return pos;


}

u_int32_t DnsParser::parserQuestions(char *data, u_int32_t pos,u_int16_t count, DnsInfo &dnsinfo,int datalen)
{
    u_int32_t start_pos = pos;
    DnsQuestion current;
    for(int i=0;i<count;i++)
    {
        current.name = NULL;
        current.name = readRRname(data,&pos,start_pos,datalen);
        dnsinfo.name = current.name;
        //data is invalied
        if(current.name == NULL || (pos+2)>=datalen)
        {
            LOG_WARN<<"dns data is wrong";
            if(current.name!=NULL)
                free(current.name);
            return 0;
        }
        current.type = (data[pos]<<8)+data[pos+1];
        current.cls = (data[pos+2]<<8)+data[pos+3];
        dnsinfo.queries.push_back(current);
        pos +=4 ;
    }
    return pos;
}

char* DnsParser::readRRname(char* data,u_int32_t* now_pos,u_int32_t id_pos,int datalen)
{
    u_int32_t i,next,pos = *now_pos;
    u_int32_t end_pos = 0;
    u_int32_t name_len = 0;
    u_int32_t steps = 0;
    char* name;
    next = pos;
    while(pos<datalen && !(next == pos && data[pos] == 0)&&steps < datalen*2)
    {
        char c = data[pos];
        steps++;
        if(next == pos)
        {
            if ((c & 0xc0) == 0xc0)
            {
                if (pos + 1 >= datalen) return 0;
                if (end_pos == 0) end_pos = pos + 1;
                pos = id_pos + ((c & 0x3f) << 8) + data[pos+1];
                next = pos;
            }
            else
            {
                name_len++;
                pos++;
                next = next + c + 1;
            }
        }
        else
        {
            if (c >= '!' && c <= 'z' && c != '\\')
                name_len++;
            else
                name_len += 4;
            pos++;
        }
    }
    if (end_pos == 0)
        end_pos = pos;
    if (steps >= 2*datalen || pos >= datalen)
        return NULL;
    name_len++;
    name = (char*)malloc(sizeof(char)*name_len);
    pos = *now_pos;
    next = pos;
    i = 0;
    while (next != pos || data[pos] != 0)
    {
        if (pos == next)
        {
            if ((data[pos] & 0xc0) == 0xc0)
            {
                pos = id_pos + ((data[pos] & 0x3f) << 8) + data[pos+1];
                next = pos;
            } else
            {
                // Add a period except for the first time.
                if (i != 0) name[i++] = '.';
                next = pos + data[pos] + 1;
                pos++;
            }
        }
        else
        {
            uint8_t c = data[pos];
            if (c >= '!' && c <= '~' && c != '\\')
            {
                name[i] = data[pos];
                i++; pos++;
            }
            else
            {
                name[i] = '\\';
                name[i+1] = 'x';
                name[i+2] = c/16 + 0x30;
                name[i+3] = c%16 + 0x30;
                if (name[i+2] > 0x39) name[i+2] += 0x27;
                if (name[i+3] > 0x39) name[i+3] += 0x27;
                i+=4;
                pos++;
            }
        }
    }
    name[i] = 0;

    *now_pos = end_pos + 1;

    return name;
}

uint32_t DnsParser::parseRRset(char *data, u_int32_t pos,u_int16_t count, DnsInfo &dnsinfo,int datalen)
{
    DnsRR  current;
    current.data = NULL;
    current.name = dnsinfo.name;
    for (int i=0; i < count; i++)
    {
        // Create and clear the data in a new dns_rr object.
        if(current.data!=NULL)
            current.data = NULL;
        pos = parseRR(data,pos,current,datalen);
        // If a non-recoverable error occurs when parsing an rr,
        // we can only return what we've got and give up.
        if (pos == 0)
        {
            return 0;
        }
        if(current.type == 1)
            dnsinfo.answers.push_back(current);
    }
    return pos;
}

uint32_t DnsParser::parseRR(char* data,uint32_t pos,DnsRR& rr,int datalen)
{
    int i;
    uint32_t rr_start = pos;

    rr.data = NULL;
    if(rr.name == NULL)
        rr.name = readRRname(data, &pos, rr_start, datalen);
    else
        pos+=2;
    // Handle a bad rr name.
    // We still want to print the rest of the escaped rr data.
    if (rr.name == NULL)
    {
        LOG_INFO<< "Bad rr name";
        rr.type = 0;
        rr.cls = 0;
        rr.ttl = 0;
        return 0;
    }

    if ((datalen - pos) < 10 )
        return 0;

    rr.type = (data[pos] << 8) + data[pos+1];
    rr.rdlength = (data[pos+8] << 8) + data[pos + 9];
    // Handle edns opt RR's differently.
    if (rr.type == 41)
    {
        rr.cls = 0;
        rr.ttl = 0;
        // We'll leave the parsing of the special EDNS opt fields to
        // our opt rdata parser.
        pos = pos + 2;
    }
    else
    {
        // The normal case.
        rr.cls = (data[pos+2] << 8) + data[pos+3];
        rr.ttl = 0;
        for (i=0; i<4; i++)
            rr.ttl = (rr.ttl << 8) + data[pos+4+i];
        // Retrieve the correct parser function.
        pos = pos + 10;
    }
    // Make sure the data for the record is actually there.
    // If not, escape and print the raw data.
    if (datalen < (rr_start + 10 + rr.rdlength))
    {
        return 0;
    }
    // Parse the resource record data.
    if(rr.type == 1)
        rr.data = A(data,pos,rr.rdlength);
    return pos + rr.rdlength;
}

char * DnsParser::A(char* packet, uint32_t pos, u_int16_t rdlength)
{
    char * data = (char *)malloc(sizeof(char)*16);

    if (rdlength != 4)
    {
        free(data);
        LOG_INFO<<"the type A dns answer has wrong format";
        return NULL;
    }
    sprintf(data, "%d.%d.%d.%d", (u_char)packet[pos], (u_char)packet[pos+1], (u_char)packet[pos+2], (u_char)packet[pos+3]);
    return data;
}

std::string to_string(const DnsRequest& dnsrequest)
{
    char data[20];
    std::string temp;
    sprintf(data,"%u",dnsrequest.timeStamp.tv_sec);
    temp.append(data);
    temp.append("\t");
    char src_ip_address[30];
    inet_ntop(AF_INET,&dnsrequest.srcip,src_ip_address,30);
    char dst_ip_address[30];
    inet_ntop(AF_INET,&dnsrequest.dstip,dst_ip_address,30);
    temp.append(src_ip_address);
    temp.append("\t");
    temp.append(dst_ip_address);
    temp.append("\t");
    temp.append(dnsrequest.Qname);
    temp.append("\t");
    sprintf(data,"%u",dnsrequest.Qclass);
    temp.append(data);
    temp.append("\t");
    sprintf(data,"%u",dnsrequest.Qtype);
    temp.append(data);
    temp.append("\n");
    return temp;

}

std::string to_string(const DnsResponse& dnsresponse)
{
    char data[20];
    std::string temp;
    sprintf(data,"%u",dnsresponse.timeStamp.tv_sec);
    temp.append(data);
    temp.append("\t");
    char src_ip_address[30];
    inet_ntop(AF_INET,&dnsresponse.srcip,src_ip_address,30);
    char dst_ip_address[30];
    inet_ntop(AF_INET,&dnsresponse.dstip,dst_ip_address,30);
    temp.append(src_ip_address);
    temp.append("\t");
    temp.append(dst_ip_address);
    temp.append("\t");
    temp.append(dnsresponse.Rname);
    temp.append("\t");
    sprintf(data,"%u",dnsresponse.Rclass);
    temp.append(data);
    temp.append("\t");
    sprintf(data,"%u",dnsresponse.Rtype);
    temp.append(data);
    temp.append("\t");
    sprintf(data,"%u",dnsresponse.ttl);
    temp.append(data);
    temp.append("\t");
    temp += dnsresponse.result;
    temp.append("\n");
    return temp;
}


