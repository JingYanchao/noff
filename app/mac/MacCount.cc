//
// Created by root on 17-5-9.
//

#include "MacCount.h"
#include <net/ethernet.h>
#include <muduo/base/Logging.h>

void MacCount::processMac(const pcap_pkthdr * header, const u_char * data, timeval timeStamp)
{
    if (header->caplen <= sizeof(ether_header)) {
        return;
    }

    ether_header* machdr = (ether_header*)data;

    //LOG_INFO<<machdr->ether_dhost[0]<<" "<<machdr->ether_dhost[1]<<" "<<machdr->ether_dhost[2]<<" "<<machdr->ether_dhost[3]<<" "<<machdr->ether_dhost[4]<<" "<<machdr->ether_dhost[5];

    int cmpRes = macCompare(machdr->ether_dhost);
    if(cmpRes == 1)
    {
        macInfo.inputstream += header->caplen;
    }
    else if(cmpRes == 2)
    {
        macInfo.outputstream += header->caplen;
    }

    // when it is at regular time
    if(timer.checkTime(timeStamp))
    {
        for(auto& func:etherCallback_)
        {
            func(macInfo);
        }
        macInfo.inputstream = 0;
        macInfo.outputstream = 0;
    }
}

int MacCount::macCompare(u_int8_t *macArray)
{
    int flag = 1;
    for(int i=0; i<6; i++)
    {
        if(macArray[i]!=MAC1[i])
        {
            flag = 0;
            break;
        }
    }
    if(flag!=0)
        return flag;
    flag = 2;
    for(int i=0; i<6; i++)
    {
        if(macArray[i]!=MAC2[i]) 
        {
            flag = 0;
            break;
        }

    }
    return flag;
}

std::string to_string(const MacInfo& macInfo)
{
    std::string temp;
    char buf[30];
    sprintf(buf,"%lld",macInfo.inputstream/1000);
    temp.append(buf);
    temp.append("\t");
    sprintf(buf,"%lld",macInfo.outputstream/1000);
    temp.append(buf);
    return temp;
}


