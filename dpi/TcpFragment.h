//
// Created by jyc on 17-4-17.
//

#ifndef NOFF_TCPFRAGMENT_H
#define NOFF_TCPFRAGMENT_H

#include "Hash.h"
#include <stdlib.h>
#include <functional>
#include <vector>

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

struct lurker_node
{
    void (*item)();
    void *data;
    char whatto;
    struct lurker_node *next;
};

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
struct tcp_timeout
{
    struct tcp_stream *a_tcp;
    struct timeval timeout;
    struct tcp_timeout *next;
    struct tcp_timeout *prev;
};

struct tuple4
{
    u_short source;
    u_short dest;
    u_int saddr;
    u_int daddr;
};

struct half_stream
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

struct tcp_stream
{
    struct tuple4 addr;
    char nids_state;
    struct lurker_node *listeners;
    struct half_stream client;
    struct half_stream server;
    struct tcp_stream *next_node;
    struct tcp_stream *prev_node;
    int hash_index;
    struct tcp_stream *next_time;
    struct tcp_stream *prev_time;
    int read;
    struct tcp_stream *next_free;
    void *user;
    long ts;
};

class TcpFragment
{
public:
    typedef std::function<void()> TcpCallback;
    typedef std::function<void()> TcptimeoutCallback;
    void addTcpCallback(TcpCallback& cb)
    {
        tcpCallbacks_.push_back(cb);
    }

    void addTcptimeoutCallback(TcptimeoutCallback& cb)
    {
        tcptimeoutCallback_.push_back(cb);
    }
    TcpFragment(int);
    ~TcpFragment();


    void process_tcp(u_char *, int);
    void tcp_check_timeouts();

private:
    std::vector<TcpCallback>    tcpCallbacks_;
    std::vector<TcpCallback>    tcptimeoutCallback_;

    struct tcp_stream **tcp_stream_table;
    struct tcp_stream *streams_pool;
    int tcp_num = 0;
    int tcp_stream_table_size;
    int max_stream;
    struct tcp_stream *tcp_latest = 0, *tcp_oldest = 0;
    struct tcp_stream *free_streams;
    struct ip *ugly_iphdr;
    struct tcp_timeout *nids_tcp_timeouts = 0;
    Hash hash;

    int tcp_init(int);
    void tcp_exit(void);
    void nids_free_tcp_stream(struct tcp_stream * a_tcp);
    void purge_queue(struct half_stream * h);
    void del_tcp_closing_timeout(struct tcp_stream * a_tcp);
    void add_tcp_closing_timeout(struct tcp_stream * a_tcp);
    void add2buf(struct half_stream * rcv, char *data, int datalen);
    void add_from_skb(struct tcp_stream * a_tcp, struct half_stream * rcv,
                      struct half_stream * snd,
                      u_char *data, int datalen,
                      u_int this_seq, char fin, char urg, u_int urg_ptr);
    void tcp_queue(struct tcp_stream * a_tcp, struct tcphdr * this_tcphdr,
                   struct half_stream * snd, struct half_stream * rcv,
                   char *data, int datalen, int skblen);
    int get_ts(struct tcphdr * this_tcphdr, unsigned int * ts);
    int get_wscale(struct tcphdr * this_tcphdr, unsigned int * ws);
    void prune_queue(struct half_stream * rcv, struct tcphdr * this_tcphdr);
    void handle_ack(struct half_stream * snd, u_int acknum);
    void add_new_tcp(struct tcphdr * this_tcphdr, struct ip * this_iphdr);
    tcp_stream *find_stream(struct tcphdr * this_tcphdr, struct ip * this_iphdr, int *from_client);
    tcp_stream *nids_find_tcp_stream(struct tuple4 *addr);


};



#endif //NOFF_TCPFRAGMENT_H
