//
// Created by jyc on 17-4-18.
//

#ifndef TCPFRAGMENT_TCPFRAGMENT_H
#define TCPFRAGMENT_TCPFRAGMENT_H

#include "Util.h"
#include "Hash.h"
#include <vector>
#include <netinet/ip.h>
#include <functional>
#include <set>
#include <list>
#include <unordered_map>
#include <netinet/tcp.h>
#include <stdlib.h>


#define FIN_SENT 120
#define FIN_CONFIRMED 121

#define EXP_SEQ (snd->first_data_seq + rcv->count + rcv->urg_count)

struct Skbuff
{
    void *data;
    u_int len;
    u_int truesize;
    u_int urg_ptr;

    char fin;
    char urg;
    u_int seq;
    u_int ack;
};

struct HalfStream
{
    char state;
    int offset;
    int count;
    int count_new;
    int bufsize;
    int rmem_alloc;
    int urg_count;
    u_int acked;
    u_int seq;
    u_int ack_seq;
    u_int first_data_seq;
    u_char urgdata;
    u_char count_new_urg;
    u_char urg_seen;
    u_int urg_ptr;
    u_short window;
    u_char wscale_on;//窗口扩展选项是否打开
    u_int wscale;
    std::list<Skbuff> fraglist;
};

struct TcpStream
{
    int hash_index;
    tuple4 addr;
    HalfStream client;
    HalfStream server;
    int isconnnection;
    long ts;
};

struct Timeout
{
    TcpStream *a_tcp;
    timeval time;
    bool operator<(const Timeout& b) const
    {
        return time.tv_sec< b.time.tv_sec;
    }
    bool operator==(const Timeout& b) const
    {
        return a_tcp == b.a_tcp;
    }
};

class TcpFragment : muduo::noncopyable
{
public:
    typedef std::function<void(TcpStream*,timeval)> TcpCallback;
    typedef std::function<void(TcpStream*,timeval,u_char*,int,int)> DataCallback;

    TcpFragment();
    ~TcpFragment();

    void addTcpcloseCallback(const TcpCallback& cb)
    {
        tcpcloseCallbacks_.push_back(cb);
    }

    void addTcptimeoutCallback(const TcpCallback& cb)
    {
        tcptimeoutCallback_.push_back(cb);
    }

    void addRstCallback(const TcpCallback& cb)
    {
        tcprstCallback_.push_back(cb);
    }

    void addConnectionCallback(const TcpCallback& cb)
    {
        tcpconnectionCallback_.push_back(cb);
    }

    void addDataCallback(const DataCallback& cb)
    {
        tcpdataCallback_.push_back(cb);
    }

    void processTcp(ip *,int,timeval);

    void clearAllStream(timeval timeStamp)
    {
        for (auto& stream : tcphashmap_) {
            if (stream.second.isconnnection) {
                for (auto &func : tcptimeoutCallback_) {
                    func(&stream.second, timeStamp);
                }
            }
        }
        tcpExit();
    }

private:
    std::vector<TcpCallback>            tcpcloseCallbacks_;
    std::vector<TcpCallback>            tcptimeoutCallback_;
    std::vector<TcpCallback>            tcprstCallback_;
    std::vector<TcpCallback>            tcpconnectionCallback_;
    std::vector<DataCallback>           tcpdataCallback_;
    std::set<Timeout>                   finTimeoutSet_;
    std::set<Timeout>                   tcpTimeoutSet_;
    std::unordered_multimap<int, TcpStream>   tcphashmap_;
    Hash hash;
    size_t tcpStreamTableSize_;
    size_t tcpNum = 0;

    int tcpInit(int);
    void tcpExit(void);
    void tcpChecktimeouts(timeval);
    TcpStream* findStream(tcphdr *this_tcphdr, ip *this_iphdr, int *from_client);
    TcpStream* findStream_aux(tuple4 addr);
    void addNewtcp(tcphdr *this_tcphdr, ip *this_iphdr,timeval timeStamp);
    void freeTcpstream(TcpStream *a_tcp);
    int getWscale(tcphdr *this_tcphdr, unsigned int *ws);//tcp选项,用于获取窗口扩大因子
    void handleAck(HalfStream *snd, u_int acknum);
    void tcpQueue(TcpStream *a_tcp, tcphdr *this_tcphdr, HalfStream *snd, HalfStream *rcv, char *data, int datalen, int skblen,timeval timeStamp);
    void addFromskb(TcpStream *a_tcp, HalfStream *rcv, HalfStream *snd,
                                 u_char *data, int datalen,
                                 u_int this_seq, char fin, char urg, u_int urg_ptr,timeval timeStamp);
    void notify(TcpStream * a_tcp, HalfStream * rcv,timeval timeStamp,u_char*,int);
    void addFintimeout(TcpStream *a_tcp,timeval timeStamp);
    void delFintimeout(TcpStream *a_tcp);
    void addTcptimeout(TcpStream *a_tcp,timeval timeStamp);
    void delTcptimeout(TcpStream *a_tcp);
    void updateTcptimeout(TcpStream *a_tcp,timeval timeStamp);
    void purgeQueue(HalfStream *h);
    void freeTcpData(TcpStream *a_tcp);
};

#endif //TCPFRAGMENT_TCPFRAGMENT_H