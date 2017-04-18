//
// Created by jyc on 17-4-17.
//

#ifndef NOFF_TCPFRAGMENT_H
#define NOFF_TCPFRAGMENT_H

#include "Hash.h"
#include "util.h"
#include <stdlib.h>
#include <functional>
#include <vector>
#include <netinet/ip.h>

# define NIDS_JUST_EST 1
# define NIDS_DATA 2
# define NIDS_CLOSE 3
# define NIDS_RESET 4
# define NIDS_TIMED_OUT 5
# define NIDS_EXITING   6	/* nids is exiting; last chance to get data */


#define FIN_SENT 120
#define FIN_CONFIRMED 121
#define COLLECT_cc 1
#define COLLECT_sc 2
#define COLLECT_ccu 4
#define COLLECT_scu 8

#define EXP_SEQ (snd->first_data_seq + rcv->count + rcv->urg_count)

struct skbuff
{
//万年不变的next和prev，这向我们昭示了这是一个双向队列。
// 对于每个TCP会话（ip:端口<- ->ip:端口）都要维护两个skbuf队列（每个方向都有一个嘛）
// 每个skbuf对应网络上的一个IP包，TCP流就是一个接一个的IP包。
    struct skbuff *next;
    struct skbuff *prev;

    void *data;
    u_int len;
    u_int truesize;
    u_int urg_ptr;

    char fin;
    char urg;
    u_int seq;
    u_int ack;
};

//超时链表
struct tcpTimeout
{
    struct tcpStream *a_tcp;
    struct timeval timeout;
    struct tcpTimeout *next;
    struct tcpTimeout *prev;
};

struct halfStream
{
    char state;
    char collect;
    char collect_urg;

    char *data;
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
    u_char ts_on;
    u_char wscale_on;
    u_int curr_ts;
    u_int wscale;
    struct skbuff *list;
    struct skbuff *listtail;
};

struct tcpStream
{
    struct tuple4 addr;
    char nids_state;
    struct halfStream client;
    struct halfStream server;
    struct tcpStream *next_node;
    struct tcpStream *prev_node;
    int hash_index;
    struct tcpStream *next_time;
    struct tcpStream *prev_time;
    int read;
    struct tcpStream *next_free;
    void *user;
    long ts;
};

class TcpFragment
{
public:
    typedef std::function<void(tcpStream*,timeval)> TcpCallback;


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

    void addDataCallback(const TcpCallback& cb)
    {
        tcpdataCallback_.push_back(cb);
    }

    TcpFragment();
    ~TcpFragment();


    void processTcp(ip *,int,timeval);
    void tcpChecktimeouts(timeval);

private:
    std::vector<TcpCallback>        tcpcloseCallbacks_;
    std::vector<TcpCallback>        tcptimeoutCallback_;
    std::vector<TcpCallback>        tcprstCallback_;
    std::vector<TcpCallback>        tcpconnectionCallback_;
    std::vector<TcpCallback>        tcpdataCallback_;

    struct tcpStream **tcpStreamTable;
    struct tcpStream *streamsPool;
    int tcpNum = 0;
    int tcpStreamTableSize;
    int maxStream;
    struct tcpStream *tcpLatest = 0, *tcpOldest = 0;
    ip* uglyIphdr;
    struct tcpStream *freeStreams;
    struct tcpTimeout *nidsTcpTimeouts = 0;
    Hash hash;

    int tcpInit(int);
    void tcpExit(void);
    void notify(struct tcpStream * a_tcp, struct halfStream * rcv,timeval);
    void nidsFreetcpstream(struct tcpStream *a_tcp);
    void purgeQueue(struct halfStream *h);
    void delTcpclosingtimeout(struct tcpStream *a_tcp);
    void addTcpclosingtimeout(struct tcpStream *a_tcp,timeval);
    void add2buf(struct halfStream * rcv, char *data, int datalen);
    void addFromskb(struct tcpStream *a_tcp, struct halfStream *rcv,
                    struct halfStream *snd,
                    u_char *data, int datalen,
                    u_int this_seq, char fin, char urg, u_int urg_ptr,timeval);
    void tcpQueue(struct tcpStream *a_tcp, struct tcphdr *this_tcphdr,
                  struct halfStream *snd, struct halfStream *rcv,
                  char *data, int datalen, int skblen,timeval);
    int getTs(struct tcphdr *this_tcphdr, unsigned int *ts);
    int getWscale(struct tcphdr *this_tcphdr, unsigned int *ws);
    void pruneQueue(struct halfStream *rcv, struct tcphdr *this_tcphdr);
    void handleAck(struct halfStream *snd, u_int acknum);
    void addNewtcp(struct tcphdr *this_tcphdr, struct ip *this_iphdr,timeval);
    tcpStream *findStream(struct tcphdr *this_tcphdr, struct ip *this_iphdr, int *from_client);
    tcpStream *nidsFindtcpStream(struct tuple4 *addr);
};
#endif //NOFF_TCPFRAGMENT_H
