//
// Created by jyc on 17-4-17.
//
#include "TcpFragment.h"
#include "util.h"
#include <muduo/base/Logging.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <netinet/in.h>

int TcpFragment::tcp_init(int size)
{
    int i;
    struct tcp_timeout *tmp;

    if (!size)
        return 0;

    //初始化全局tcp会话的哈希表
    tcp_stream_table_size = size;
    tcp_stream_table = (tcp_stream **) calloc(tcp_stream_table_size, sizeof(char *));
    if (!tcp_stream_table)
    {
        LOG_ERROR<<"the memory of tcp_stream_table is invalid";
        exit(1);
    }
    //设置最大会话数，为了哈希的效率，哈希表的元素个数上限设为3/4表大小
    max_stream = 3 * tcp_stream_table_size / 4;

    //先将max_stream个tcp会话结构体申请好，放着（避免后面陆陆续续申请浪费时间）。
    streams_pool = (struct tcp_stream *) malloc((max_stream + 1) * sizeof(struct tcp_stream));
    if (!streams_pool)
    {
        LOG_ERROR<<"the memory of streams_pool is invalid";
        exit(1);
    }

    //ok，将这个数组初始化成链表
    for (i = 0; i < max_stream; i++)
        streams_pool[i].next_free = &(streams_pool[i + 1]);

    streams_pool[max_stream].next_free = 0;
    free_streams = streams_pool;

    //清空原来的所有定时器
    while (nids_tcp_timeouts)
    {
        tmp = nids_tcp_timeouts->next;
        free(nids_tcp_timeouts);
        nids_tcp_timeouts = tmp;
    }
    return 0;
}

void TcpFragment::tcp_exit()
{
    int i;
    struct lurker_node *j;
    struct tcp_stream *a_tcp, *t_tcp;

    if (!tcp_stream_table || !streams_pool)
        return;
    for (i = 0; i < tcp_stream_table_size; i++)
    {
        a_tcp = tcp_stream_table[i];
        while(a_tcp)
        {
            t_tcp = a_tcp;
            a_tcp = a_tcp->next_node;
            for (j = t_tcp->listeners; j; j = j->next)
            {
                t_tcp->nids_state = NIDS_EXITING;
                (j->item)(t_tcp, &j->data);
            }
            nids_free_tcp_stream(t_tcp);
        }
    }
    free(tcp_stream_table);
    tcp_stream_table = NULL;
    free(streams_pool);
    streams_pool = NULL;
    /* FIXME: anything else we should free? */
    /* yes plz.. */
    tcp_latest = tcp_oldest = NULL;
    tcp_num = 0;
}

void TcpFragment::nids_free_tcp_stream(struct tcp_stream * a_tcp)
{
    int hash_index = a_tcp->hash_index;
    struct lurker_node *i, *j;

    del_tcp_closing_timeout(a_tcp);
    purge_queue(&a_tcp->server);
    purge_queue(&a_tcp->client);

    if (a_tcp->next_node)
        a_tcp->next_node->prev_node = a_tcp->prev_node;
    if (a_tcp->prev_node)
        a_tcp->prev_node->next_node = a_tcp->next_node;
    else
        tcp_stream_table[hash_index] = a_tcp->next_node;
    if (a_tcp->client.data)
        free(a_tcp->client.data);
    if (a_tcp->server.data)
        free(a_tcp->server.data);
    if (a_tcp->next_time)
        a_tcp->next_time->prev_time = a_tcp->prev_time;
    if (a_tcp->prev_time)
        a_tcp->prev_time->next_time = a_tcp->next_time;
    if (a_tcp == tcp_oldest)
        tcp_oldest = a_tcp->prev_time;
    if (a_tcp == tcp_latest)
        tcp_latest = a_tcp->next_time;

    i = a_tcp->listeners;

    while (i)
    {
        j = i->next;
        free(i);
        i = j;
    }
    a_tcp->next_free = free_streams;
    free_streams = a_tcp;
    tcp_num--;
}

void TcpFragment::purge_queue(struct half_stream * h)
{
    struct skbuff *tmp, *p = h->list;

    while (p)
    {
        free(p->data);
        tmp = p->next;
        free(p);
        p = tmp;
    }
    h->list = h->listtail = 0;
    h->rmem_alloc = 0;
}

void TcpFragment::del_tcp_closing_timeout(struct tcp_stream * a_tcp)
{
    struct tcp_timeout *to;

    for (to = nids_tcp_timeouts; to; to = to->next)
        if (to->a_tcp == a_tcp)
            break;
    if (!to)
        return;
    if (!to->prev)
        nids_tcp_timeouts = to->next;
    else
        to->prev->next = to->next;
    if (to->next)
        to->next->prev = to->prev;
    free(to);
}


void TcpFragment::add_tcp_closing_timeout(struct tcp_stream * a_tcp)
{
    struct tcp_timeout *to;
    struct tcp_timeout *newto;

    newto = (tcp_timeout *) malloc(sizeof (struct tcp_timeout));
    if (!newto)
    {
        LOG_ERROR<<"the memory of add_tcp_closing_timeout is invalid";
        exit(1);
    }
    newto->a_tcp = a_tcp;
    timeval now_time;
    timezone now_zone;
    gettimeofday(&now_time,&now_zone);
    newto->timeout.tv_sec = now_time.tv_sec + 10;
    newto->prev = 0;
    for (newto->next = to = nids_tcp_timeouts; to; newto->next = to = to->next)
    {
        if (to->a_tcp == a_tcp)
        {
            free(newto);
            return;
        }
        if (to->timeout.tv_sec > newto->timeout.tv_sec)
            break;
        newto->prev = to;
    }
    if (!newto->prev)
        nids_tcp_timeouts = newto;
    else
        newto->prev->next = newto;
    if (newto->next)
        newto->next->prev = newto;
}

void TcpFragment::tcp_check_timeouts()
{
    struct tcp_timeout *to;
    struct tcp_timeout *next;
    struct lurker_node *i;
    timeval now;
    timezone now_zone;
    gettimeofday(&now,&now_zone);
    for (to = nids_tcp_timeouts; to; to = next)
    {
        printf("%d %d\n",now.tv_sec,to->timeout.tv_sec);
        if (now.tv_sec < to->timeout.tv_sec)
            return;
        //如果时间到达的话,就将tcp的数据上传
        to->a_tcp->nids_state = NIDS_TIMED_OUT;
        //进行回调
        for(auto func:tcptimeoutCallback_)
        {
            func();
        }
        next = to->next;
        nids_free_tcp_stream(to->a_tcp);
    }
}

void TcpFragment::add2buf(struct half_stream * rcv, char *data, int datalen)
{
    int toalloc;

    if (datalen + rcv->count - rcv->offset > rcv->bufsize)
    {
        if (!rcv->data)
        {
            if (datalen < 2048)
                toalloc = 4096;
            else
                toalloc = datalen * 2;
            rcv->data = (char *) malloc(toalloc);
            rcv->bufsize = toalloc;
        }
        else
        {
            if (datalen < rcv->bufsize)
                toalloc = 2 * rcv->bufsize;
            else
                toalloc = rcv->bufsize + 2*datalen;
            rcv->data = (char *) realloc(rcv->data, toalloc);
            rcv->bufsize = toalloc;
        }
        if (!rcv->data)
        {
            LOG_WARN<<"no memory for add2buf";
        }

    }
    memmove(rcv->data + rcv->count - rcv->offset, data, datalen);
    rcv->count_new = datalen;
    rcv->count += datalen;
}

void TcpFragment::add_from_skb(struct tcp_stream * a_tcp, struct half_stream * rcv,
             struct half_stream * snd,
             u_char *data, int datalen,
             u_int this_seq, char fin, char urg, u_int urg_ptr)
{
    u_int lost = EXP_SEQ - this_seq;
    int to_copy, to_copy2;

    if (urg && after(urg_ptr, EXP_SEQ - 1) &&
        (!rcv->urg_seen || after(urg_ptr, rcv->urg_ptr)))
    {
        rcv->urg_ptr = urg_ptr;
        rcv->urg_seen = 1;
    }
    if (rcv->urg_seen && after(rcv->urg_ptr + 1, this_seq + lost) &&
        before(rcv->urg_ptr, this_seq + datalen))
    {
        to_copy = rcv->urg_ptr - (this_seq + lost);
        if (to_copy > 0) {
            if (rcv->collect)
            {
                add2buf(rcv, (char *)(data + lost), to_copy);
            }
            else
            {
                rcv->count += to_copy;
                rcv->offset = rcv->count; /* clear the buffer */
            }
        }
        rcv->urgdata = data[rcv->urg_ptr - this_seq];
        rcv->count_new_urg = 1;
        rcv->count_new_urg = 0;
        rcv->urg_seen = 0;
        rcv->urg_count++;
        to_copy2 = this_seq + datalen - rcv->urg_ptr - 1;
        if (to_copy2 > 0)
        {
            if (rcv->collect)
            {
                add2buf(rcv, (char *)(data + lost + to_copy + 1), to_copy2);
            }
            else
            {
                rcv->count += to_copy2;
                rcv->offset = rcv->count; /* clear the buffer */
            }
        }
    }
    else
    {
        if (datalen - lost > 0)
        {
            if (rcv->collect)
            {
                add2buf(rcv, (char *)(data + lost), datalen - lost);
            }
            else
            {
                rcv->count += datalen - lost;
                rcv->offset = rcv->count; /* clear the buffer */
            }
        }
    }
    if (fin)
    {
        snd->state = FIN_SENT;
        if (rcv->state == TCP_CLOSING)
            add_tcp_closing_timeout(a_tcp);
    }
}

void TcpFragment::tcp_queue(struct tcp_stream * a_tcp, struct tcphdr * this_tcphdr,
          struct half_stream * snd, struct half_stream * rcv,
          char *data, int datalen, int skblen)
{
    u_int this_seq = ntohl(this_tcphdr->th_seq);
    struct skbuff *pakiet, *tmp;

    /*
     * Did we get anything new to ack?
     */
    //EXP_SEQ是目前已集齐的数据流水号，我们希望收到从这里开始的数据
    //先判断数据是不是在EXP_SEQ之前开始
    if (!after(this_seq, EXP_SEQ))
    {
        //再判断数据长度是不是在EXP_SEQ之后，如果是，说明有新数据，否则是重发的包，无视之
        if (after(this_seq + datalen + (this_tcphdr->th_flags & TH_FIN), EXP_SEQ))
        {
            /* the packet straddles our window end */
            get_ts(this_tcphdr, &snd->curr_ts);
            //ok，更新集齐的数据区，值得一提的是add_from_skb函数一旦发现集齐了一段数据之后
            //便立刻调用notify函数，在notify函数里面将数据推给回调方
            add_from_skb(a_tcp, rcv, snd, (u_char *)data, datalen, this_seq,
                         (this_tcphdr->th_flags & TH_FIN),
                         (this_tcphdr->th_flags & TH_URG),
                         ntohs(this_tcphdr->th_urp) + this_seq - 1);
            /*
             * Do we have any old packets to ack that the above
             * made visible? (Go forward from skb)
             */
            //此时EXP_SEQ有了变化了，看看缓冲区里的包有没有符合条件能用同样的方法处理掉的
            //有就处理掉，然后释放
            pakiet = rcv->list;
            while (pakiet)
            {
                if (after(pakiet->seq, EXP_SEQ))
                    break;
                if (after(pakiet->seq + pakiet->len + pakiet->fin, EXP_SEQ))
                {
                    add_from_skb(a_tcp, rcv, snd, (u_char *) pakiet->data,
                                 pakiet->len, pakiet->seq, pakiet->fin, pakiet->urg,
                                 pakiet->urg_ptr + pakiet->seq - 1);
                }
                rcv->rmem_alloc -= pakiet->truesize;
                if (pakiet->prev)
                    pakiet->prev->next = pakiet->next;
                else
                    rcv->list = pakiet->next;
                if (pakiet->next)
                    pakiet->next->prev = pakiet->prev;
                else
                    rcv->listtail = pakiet->prev;
                tmp = pakiet->next;
                free(pakiet->data);
                free(pakiet);
                pakiet = tmp;
            }
        }
        else
            return;
    }
        //这里说明现在这个包是个乱序到达的（数据开始点超过了EXP_SEQ），放到缓冲区等待处理，注意保持缓冲区有序
    else
    {
        struct skbuff *p = rcv->listtail;

        pakiet = (skbuff*)malloc(sizeof(skbuff));
        pakiet->truesize = skblen;
        rcv->rmem_alloc += pakiet->truesize;
        pakiet->len = datalen;
        pakiet->data = malloc(datalen);
        if (!pakiet->data)
        {
            LOG_WARN<<"no memory for tcp_queue";
        }
        memmove(pakiet->data, data, datalen);
        pakiet->fin = (this_tcphdr->th_flags & TH_FIN);
        /* Some Cisco - at least - hardware accept to close a TCP connection
         * even though packets were lost before the first TCP FIN packet and
         * never retransmitted; this violates RFC 793, but since it really
         * happens, it has to be dealt with... The idea is to introduce a 10s
         * timeout after TCP FIN packets were sent by both sides so that
         * corresponding libnids resources can be released instead of waiting
         * for retransmissions which will never happen.  -- Sebastien Raveau
         */
        if (pakiet->fin)
        {
            snd->state = TCP_CLOSING;
            if (rcv->state == FIN_SENT || rcv->state == FIN_CONFIRMED)
                add_tcp_closing_timeout(a_tcp);
        }
        pakiet->seq = this_seq;
        pakiet->urg = (this_tcphdr->th_flags & TH_URG);
        pakiet->urg_ptr = ntohs(this_tcphdr->th_urp);
        for (;;)
        {
            if (!p || !after(p->seq, this_seq))
                break;
            p = p->prev;
        }
        if (!p)
        {
            pakiet->prev = 0;
            pakiet->next = rcv->list;
            if (rcv->list)
                rcv->list->prev = pakiet;
            rcv->list = pakiet;
            if (!rcv->listtail)
                rcv->listtail = pakiet;
        }
        else
        {
            pakiet->next = p->next;
            p->next = pakiet;
            pakiet->prev = p;
            if (pakiet->next)
                pakiet->next->prev = pakiet;
            else
                rcv->listtail = pakiet;
        }
    }
}


int TcpFragment::get_ts(struct tcphdr * this_tcphdr, unsigned int * ts)
{
    int len = 4 * this_tcphdr->th_off;
    unsigned int tmp_ts;
    unsigned char * options = (unsigned char*)(this_tcphdr + 1);
    int ind = 0, ret = 0;
    while (ind <=  len - (int)sizeof (struct tcphdr) - 10 )
        switch (options[ind])
        {
            case 0: /* TCPOPT_EOL */
                return ret;
            case 1: /* TCPOPT_NOP */
                ind++;
                continue;
            case 8: /* TCPOPT_TIMESTAMP */
                memcpy((char*)&tmp_ts, options + ind + 2, 4);
                *ts=ntohl(tmp_ts);
                ret = 1;
                /* no break, intentionally */
            default:
                if (options[ind+1] < 2 ) /* "silly option" */
                    return ret;
                ind += options[ind+1];
        }

    return ret;
}

int TcpFragment::get_wscale(struct tcphdr * this_tcphdr, unsigned int * ws)
{
    int len = 4 * this_tcphdr->th_off;
    unsigned int tmp_ws;
    unsigned char * options = (unsigned char*)(this_tcphdr + 1);
    int ind = 0, ret = 0;
    *ws=1;
    while (ind <=  len - (int)sizeof (struct tcphdr) - 3 )
        switch (options[ind])
        {
            case 0: /* TCPOPT_EOL */
                return ret;
            case 1: /* TCPOPT_NOP */
                ind++;
                continue;
            case 3: /* TCPOPT_WSCALE */
                tmp_ws=options[ind+2];
                if (tmp_ws>14)
                    tmp_ws=14;
                *ws=1<<tmp_ws;
                ret = 1;
                /* no break, intentionally */
            default:
                if (options[ind+1] < 2 ) /* "silly option" */
                    return ret;
                ind += options[ind+1];
        }

    return ret;
}

void TcpFragment::prune_queue(struct half_stream * rcv, struct tcphdr * this_tcphdr)
{
    struct skbuff *tmp, *p = rcv->list;
    while (p)
    {
        free(p->data);
        tmp = p->next;
        free(p);
        p = tmp;
    }
    rcv->list = rcv->listtail = 0;
    rcv->rmem_alloc = 0;
}

void TcpFragment::handle_ack(struct half_stream * snd, u_int acknum)
{
    int ackdiff;

    ackdiff = acknum - snd->ack_seq;
    if (ackdiff > 0)
    {
        snd->ack_seq = acknum;
    }
}

void TcpFragment::add_new_tcp(struct tcphdr * this_tcphdr, struct ip * this_iphdr)
{
    struct tcp_stream *tolink;
    struct tcp_stream *a_tcp;
    int hash_index;
    struct tuple4 addr;

    addr.source = ntohs(this_tcphdr->th_sport);
    addr.dest = ntohs(this_tcphdr->th_dport);
    addr.saddr = this_iphdr->ip_src.s_addr;
    addr.daddr = this_iphdr->ip_dst.s_addr;
    hash_index = hash(addr.saddr,addr.source,addr.daddr,addr.dest);

    if (tcp_num > max_stream)
    {
        struct lurker_node *i;
        int orig_client_state=tcp_oldest->client.state;
        tcp_oldest->nids_state = NIDS_TIMED_OUT;
        for (i = tcp_oldest->listeners; i; i = i->next)
        {
            (i->item) (tcp_oldest, &i->data);
        }

        nids_free_tcp_stream(tcp_oldest);
        if (orig_client_state!=TCP_SYN_SENT)
        {
            LOG_DEBUG<<"the tcp state is wrong";
        }

    }

    a_tcp = free_streams;
    if (!a_tcp)
    {
        fprintf(stderr, "gdb me ...\n");
        pause();
    }
    free_streams = a_tcp->next_free;

    tcp_num++;
    tolink = tcp_stream_table[hash_index];
    memset(a_tcp, 0, sizeof(struct tcp_stream));
    a_tcp->hash_index = hash_index;
    a_tcp->addr = addr;
    a_tcp->client.state = TCP_SYN_SENT;
    a_tcp->client.seq = ntohl(this_tcphdr->th_seq) + 1;
    a_tcp->client.first_data_seq = a_tcp->client.seq;
    a_tcp->client.window = ntohs(this_tcphdr->th_win);
    a_tcp->client.ts_on = get_ts(this_tcphdr, &a_tcp->client.curr_ts);
    a_tcp->client.wscale_on = get_wscale(this_tcphdr, &a_tcp->client.wscale);
    a_tcp->server.state = TCP_CLOSE;
    a_tcp->next_node = tolink;
    a_tcp->prev_node = 0;
    timeval now;
    timezone now_zone;
    gettimeofday(&now,&now_zone);
    a_tcp->ts = now.tv_sec;
    if (tolink)
        tolink->prev_node = a_tcp;
    tcp_stream_table[hash_index] = a_tcp;
    a_tcp->next_time = tcp_latest;
    a_tcp->prev_time = 0;
    if (!tcp_oldest)
        tcp_oldest = a_tcp;
    if (tcp_latest)
        tcp_latest->prev_time = a_tcp;
    tcp_latest = a_tcp;
}

static void
add2buf(struct half_stream * rcv, char *data, int datalen)
{
    int toalloc;

    if (datalen + rcv->count - rcv->offset > rcv->bufsize)
    {
        if (!rcv->data)
        {
            if (datalen < 2048)
                toalloc = 4096;
            else
                toalloc = datalen * 2;
            rcv->data = (char *) malloc(toalloc);
            rcv->bufsize = toalloc;
        }
        else
        {
            if (datalen < rcv->bufsize)
                toalloc = 2 * rcv->bufsize;
            else
                toalloc = rcv->bufsize + 2*datalen;
            rcv->data = (char *) realloc(rcv->data, toalloc);
            rcv->bufsize = toalloc;
        }
        if (!rcv->data)
        {
            LOG_WARN<<"the rcv data is invalid";
        }

    }
    memmove(rcv->data + rcv->count - rcv->offset, data, datalen);
    rcv->count_new = datalen;
    rcv->count += datalen;
}

//查询hash表查看是否已经存在tcp流
tcp_stream * TcpFragment::find_stream(struct tcphdr * this_tcphdr, struct ip * this_iphdr, int *from_client)
{
    tuple4 this_addr, reversed;
    tcp_stream *a_tcp;

    this_addr.source = ntohs(this_tcphdr->th_sport);
    this_addr.dest = ntohs(this_tcphdr->th_dport);
    this_addr.saddr = this_iphdr->ip_src.s_addr;
    this_addr.daddr = this_iphdr->ip_dst.s_addr;
    a_tcp = nids_find_tcp_stream(&this_addr);
    if (a_tcp)
    {
        *from_client = 1;
        return a_tcp;
    }
    reversed.source = ntohs(this_tcphdr->th_dport);
    reversed.dest = ntohs(this_tcphdr->th_sport);
    reversed.saddr = this_iphdr->ip_dst.s_addr;
    reversed.daddr = this_iphdr->ip_src.s_addr;
    a_tcp = nids_find_tcp_stream(&reversed);
    if (a_tcp)
    {
        *from_client = 0;
        return a_tcp;
    }
    return 0;
}

tcp_stream * TcpFragment::nids_find_tcp_stream(struct tuple4 *addr)
{
    int hash_index;
    struct tcp_stream *a_tcp;

    hash_index = hash(addr->saddr,addr->source,addr->daddr,addr->dest);
    for (a_tcp = tcp_stream_table[hash_index];
         a_tcp && memcmp(&a_tcp->addr, addr, sizeof (struct tuple4));
         a_tcp = a_tcp->next_node);
    return a_tcp ? a_tcp : 0;
}

void TcpFragment::process_tcp(u_char * data, int skblen)//传入数据与其长度
{
    struct ip *this_iphdr = (struct ip *)data;//ip与tcp结构体见后面说明
    struct tcphdr *this_tcphdr = (struct tcphdr *)(data + 4 * this_iphdr->ip_hl);
    //计算ip部分偏移指到TCP头部
    int datalen, iplen;//数据部分长度，以及ip长度
    int from_client = 1;
    unsigned int tmp_ts;//时间戳
    struct tcp_stream *a_tcp;
    struct half_stream *snd, *rcv;//一个方向上的TCP流，TCP分为两个方向上的，一个是客户到服务端，一个是服务端到客户

    ugly_iphdr = this_iphdr;
    iplen = ntohs(this_iphdr->ip_len);
    if ((unsigned)iplen < 4 * this_iphdr->ip_hl + sizeof(struct tcphdr))
    {
        LOG_WARN<<"the length of iphdr is wrong";//指示的长度与实际的不相符，指出错误
        return;
    }

    datalen = iplen - 4 * this_iphdr->ip_hl - 4 * this_tcphdr->th_off;
    //tcp数据部分长度，去掉了TCP的头部
    //ip_hl表示ip头部长度，th_off表示tcp头部长度，datalen表示tcp数据部分长度

    if (datalen < 0)
    {
        LOG_WARN<<"the length of data is less than 0";//指示的长度与实际的不相符，指出错误
        return;
    } //数据部分小于0，发生错误，返回

    if ((this_iphdr->ip_src.s_addr | this_iphdr->ip_dst.s_addr) == 0)
    {
        LOG_WARN<<"the ip address is invalid";//指示的长度与实际的不相符，指出错误
        return;
    }


    //经过以上处，初步判断tcp包正常，进行入队操作，插入队列前，先进行此包的状态判断，判断此数据包处于何种状态
    if (!(a_tcp = find_stream(this_tcphdr, this_iphdr, &from_client)))
    {
        /*是三次握手的第一个包*/
        /*tcp里流不存在时:且tcp数据包里的(syn=1 && ack==0 && rst==0)时,添加一条tcp流*/
        /*tcp第一次握手*/
        if ((this_tcphdr->th_flags & TH_SYN) &&
            !(this_tcphdr->th_flags & TH_ACK) &&
            !(this_tcphdr->th_flags & TH_RST))
            add_new_tcp(this_tcphdr, this_iphdr);//发现新的TCP流，进行添加。
        /*第一次握手完毕返回*/
        return;
    }

    if (from_client)
    {
        snd = &a_tcp->client;
        rcv = &a_tcp->server;
    }
    else
    {
        rcv = &a_tcp->client;
        snd = &a_tcp->server;
    }

/**********************************************************************

                三次握手的第二次握手

************************************************************************/

    /*tcp 三次握手， SYN ==1，ACK==1,tcp第二次握手(server -> client的同步响应)*/

//来了一个SYN包

    if ((this_tcphdr->th_flags & TH_SYN))
    {
        //syn包是用来建立新连接的，所以，要么来自客户端且没标志（前面处理了），要么来自服务端且加ACK标志
        //所以这里只能来自服务器，检查服务器状态是否正常，不正常的话果断忽略这个包

        if (from_client)
        {
            // if timeout since previous
            if (nids_params.tcp_flow_timeout > 0 &&
                (a_tcp->ts + nids_params.tcp_flow_timeout < nids_last_pcap_header->ts.tv_sec))
            {
                if (!(this_tcphdr->th_flags & TH_ACK) &&
                    !(this_tcphdr->th_flags & TH_RST))
                {
                    // cleanup previous
                    nids_free_tcp_stream(a_tcp);
                    // start new
                    add_new_tcp(this_tcphdr, this_iphdr);
                }
            }
            return;
        }
        if (a_tcp->client.state != TCP_SYN_SENT ||
            a_tcp->server.state != TCP_CLOSE || !(this_tcphdr->th_flags & TH_ACK))
            return;
        if (a_tcp->client.seq != ntohl(this_tcphdr->th_ack))
            return;
        a_tcp->ts = nids_last_pcap_header->ts.tv_sec;
        a_tcp->server.state = TCP_SYN_RECV;
        a_tcp->server.seq = ntohl(this_tcphdr->th_seq) + 1;
        a_tcp->server.first_data_seq = a_tcp->server.seq;
        a_tcp->server.ack_seq = ntohl(this_tcphdr->th_ack);
        a_tcp->server.window = ntohs(this_tcphdr->th_win);
        if (a_tcp->client.ts_on)
        {
            a_tcp->server.ts_on = get_ts(this_tcphdr, &a_tcp->server.curr_ts);
            if (!a_tcp->server.ts_on)
                a_tcp->client.ts_on = 0;
        }
        else a_tcp->server.ts_on = 0;
        if (a_tcp->client.wscale_on) {
            a_tcp->server.wscale_on = get_wscale(this_tcphdr, &a_tcp->server.wscale);
            if (!a_tcp->server.wscale_on) {
                a_tcp->client.wscale_on = 0;
                a_tcp->client.wscale  = 1;
                a_tcp->server.wscale = 1;
            }
        } else {
            a_tcp->server.wscale_on = 0;
            a_tcp->server.wscale = 1;
        }
        return;
    }
    if (
            ! (  !datalen && ntohl(this_tcphdr->th_seq) == rcv->ack_seq  )
            &&
            ( !before(ntohl(this_tcphdr->th_seq), rcv->ack_seq + rcv->window*rcv->wscale) ||
              before(ntohl(this_tcphdr->th_seq) + datalen, rcv->ack_seq)
            )
            )
        return;

    if ((this_tcphdr->th_flags & TH_RST))
    {
        if (a_tcp->nids_state == NIDS_DATA)
        {
            struct lurker_node *i;

            a_tcp->nids_state = NIDS_RESET;
            for (i = a_tcp->listeners; i; i = i->next)
                (i->item) (a_tcp, &i->data);
        }
        nids_free_tcp_stream(a_tcp);
        return;
    }

    /* PAWS check */
    /* PAWS(防止重复报文)check 检查时间戳*/
    if (rcv->ts_on && get_ts(this_tcphdr, &tmp_ts) &&
        before(tmp_ts, snd->curr_ts))
        return;

/**********************************************************************
                        第三次握手包

        **********************************************************************
    */

    /*


    从client --> server的包

     是从三次握手的第三个包分析开始的，进行一部分数据分析，和初始化
     连接状态

    */
    if ((this_tcphdr->th_flags & TH_ACK))//如果是从客户端来的，且两边都在第二次握手的状态上
    {
        if (from_client && a_tcp->client.state == TCP_SYN_SENT &&
            a_tcp->server.state == TCP_SYN_RECV) {
            //在此情况下，流水号又对得上，好的，这个包是第三次握手包，连接建立成功
            if (ntohl(this_tcphdr->th_ack) == a_tcp->server.seq) {
                a_tcp->client.state = TCP_ESTABLISHED;
                //更新客户端状态
                a_tcp->client.ack_seq = ntohl(this_tcphdr->th_ack);
                //更新ack序号
                a_tcp->ts = nids_last_pcap_header->ts.tv_sec;
                {
                    struct proc_node *i;
                    struct lurker_node *j;
                    void *data;

                    a_tcp->server.state = TCP_ESTABLISHED;
                    a_tcp->nids_state = NIDS_JUST_EST;
                    for (i = tcp_procs; i; i = i->next)
                    {
                        //此处根据调用者的设定来判断哪些数据需要在回调时返回
                        char whatto = 0;
                        char cc = a_tcp->client.collect;
                        char sc = a_tcp->server.collect;
                        char ccu = a_tcp->client.collect_urg;
                        char scu = a_tcp->server.collect_urg;/*进入回调函数处理*/

                        (i->item) (a_tcp, &data);
                        if (cc < a_tcp->client.collect)
                            whatto |= COLLECT_cc;
                        if (ccu < a_tcp->client.collect_urg)
                            whatto |= COLLECT_ccu;
                        if (sc < a_tcp->server.collect)
                            whatto |= COLLECT_sc;
                        if (scu < a_tcp->server.collect_urg)
                            whatto |= COLLECT_scu;
                        if (nids_params.one_loop_less)
                        {
                            if (a_tcp->client.collect >=2)
                            {
                                a_tcp->client.collect=cc;
                                whatto&=~COLLECT_cc;
                            }
                            if (a_tcp->server.collect >=2 )
                            {
                                a_tcp->server.collect=sc;
                                whatto&=~COLLECT_sc;
                            }
                        }
                        /*加入监听队列，开始数据接收*/
                        if (whatto)
                        {
                            j = (lurker_node*)malloc(sizeof(lurker_node));
                            j->item = i->item;
                            j->data = data;
                            j->whatto = whatto;
                            j->next = a_tcp->listeners;
                            a_tcp->listeners = j;
                        }
                    }
                    if (!a_tcp->listeners)
                    {
                        nids_free_tcp_stream(a_tcp);
                        return;
                    }
                    a_tcp->nids_state = NIDS_DATA;
                }
            }
            // return;
        }
    }

    /*
************************************************************

                挥手过程

*************************************************************

*/

/*数据结束的包的判断*/
    if ((this_tcphdr->th_flags & TH_ACK))
    {

        handle_ack(snd, ntohl(this_tcphdr->th_ack));
        if (rcv->state == FIN_SENT)
            rcv->state = FIN_CONFIRMED;
        if (rcv->state == FIN_CONFIRMED && snd->state == FIN_CONFIRMED)
        {
            struct lurker_node *i;
            a_tcp->nids_state = NIDS_CLOSE;
            for (i = a_tcp->listeners; i; i = i->next)
                (i->item) (a_tcp, &i->data);
            nids_free_tcp_stream(a_tcp);
            return;
        }
    }
    if (datalen + (this_tcphdr->th_flags & TH_FIN) > 0)
        tcp_queue(a_tcp, this_tcphdr, snd, rcv,
                  (char *) (this_tcphdr) + 4 * this_tcphdr->th_off,
                  datalen, skblen);
    snd->window = ntohs(this_tcphdr->th_win);
    if (rcv->rmem_alloc > 65535)
        prune_queue(rcv, this_tcphdr);
    /*不存在监听者*/
    if (!a_tcp->listeners)
        nids_free_tcp_stream(a_tcp);
}

