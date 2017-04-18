//
// Created by jyc on 17-4-17.
//
#include "TcpFragment.h"
#include "Util.h"
#include <muduo/base/Logging.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <netinet/in.h>
TcpFragment::TcpFragment()
{
    LOG_INFO<<"start tcpfragment";
    int num_tcp_stream = 30001;
    tcpInit(num_tcp_stream);
}

TcpFragment::~TcpFragment()
{
    tcpExit();
}

int TcpFragment::tcpInit(int size)
{
    int i;
    struct tcpTimeout *tmp;

    if (!size)
        return 0;

    //初始化全局tcp会话的哈希表
    tcpStreamTableSize = size;
    tcpStreamTable = (tcpStream **) calloc(tcpStreamTableSize, sizeof(char *));
    if (!tcpStreamTable)
    {
        LOG_ERROR<<"the memory of tcpStreamTable is invalid";
        exit(1);
    }
    //设置最大会话数，为了哈希的效率，哈希表的元素个数上限设为3/4表大小
    maxStream = 3 * tcpStreamTableSize / 4;

    //先将max_stream个tcp会话结构体申请好，放着（避免后面陆陆续续申请浪费时间）。
    streamsPool = (tcpStream *) malloc((maxStream + 1) * sizeof(tcpStream));
    if (!streamsPool)
    {
        LOG_ERROR<<"the memory of streamsPool is invalid";
        exit(1);
    }

    //ok，将这个数组初始化成链表
    for (i = 0; i < maxStream; i++)
        streamsPool[i].next_free = &(streamsPool[i + 1]);

    streamsPool[maxStream].next_free = 0;
    freeStreams = streamsPool;

    //清空原来的所有定时器
    while (nidsTcpTimeouts)
    {
        tmp = nidsTcpTimeouts->next;
        free(nidsTcpTimeouts);
        nidsTcpTimeouts = tmp;
    }
    return 0;
}

void TcpFragment::tcpExit()
{
    int i;
    struct tcpStream *a_tcp, *t_tcp;

    if (!tcpStreamTable || !streamsPool)
        return;
    for (i = 0; i < tcpStreamTableSize; i++)
    {
        a_tcp = tcpStreamTable[i];
        while(a_tcp)
        {
            t_tcp = a_tcp;
            a_tcp = a_tcp->next_node;
            nidsFreetcpstream(t_tcp);
        }
    }
    free(tcpStreamTable);
    tcpStreamTable = NULL;
    free(streamsPool);
    streamsPool = NULL;
    /* FIXME: anything else we should free? */
    /* yes plz.. */
    tcpLatest = tcpOldest = NULL;
    tcpNum = 0;
}

void TcpFragment::nidsFreetcpstream(struct tcpStream *a_tcp)
{
    int hash_index = a_tcp->hash_index;

    delTcpclosingtimeout(a_tcp);
    purgeQueue(&a_tcp->server);
    purgeQueue(&a_tcp->client);

    if (a_tcp->next_node)
        a_tcp->next_node->prev_node = a_tcp->prev_node;
    if (a_tcp->prev_node)
        a_tcp->prev_node->next_node = a_tcp->next_node;
    else
        tcpStreamTable[hash_index] = a_tcp->next_node;
    if (a_tcp->client.data)
        free(a_tcp->client.data);
    if (a_tcp->server.data)
        free(a_tcp->server.data);
    if (a_tcp->next_time)
        a_tcp->next_time->prev_time = a_tcp->prev_time;
    if (a_tcp->prev_time)
        a_tcp->prev_time->next_time = a_tcp->next_time;
    if (a_tcp == tcpOldest)
        tcpOldest = a_tcp->prev_time;
    if (a_tcp == tcpLatest)
        tcpLatest = a_tcp->next_time;
    a_tcp->next_free = freeStreams;
    freeStreams = a_tcp;
    tcpNum--;
}

void TcpFragment::purgeQueue(struct halfStream *h)
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

void TcpFragment::delTcpclosingtimeout(struct tcpStream *a_tcp)
{
    struct tcpTimeout *to;

    for (to = nidsTcpTimeouts; to; to = to->next)
        if (to->a_tcp == a_tcp)
            break;
    if (!to)
        return;
    if (!to->prev)
        nidsTcpTimeouts = to->next;
    else
        to->prev->next = to->next;
    if (to->next)
        to->next->prev = to->prev;
    free(to);
}


void TcpFragment::addTcpclosingtimeout(struct tcpStream *a_tcp,timeval timeStamp)
{
    struct tcpTimeout *to;
    struct tcpTimeout *newto;

    newto = (tcpTimeout *) malloc(sizeof (struct tcpTimeout));
    if (!newto)
    {
        LOG_ERROR<<"the memory of addTcpclosingtimeout is invalid";
        exit(1);
    }
    newto->a_tcp = a_tcp;
    newto->timeout.tv_sec = timeStamp.tv_sec + 10;
    newto->prev = 0;
    for (newto->next = to = nidsTcpTimeouts; to; newto->next = to = to->next)
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
        nidsTcpTimeouts = newto;
    else
        newto->prev->next = newto;
    if (newto->next)
        newto->next->prev = newto;
}

void TcpFragment::tcpChecktimeouts(timeval timeStamp)
{
    struct tcpTimeout *to;
    struct tcpTimeout *next;
    for (to = nidsTcpTimeouts; to; to = next)
    {
        if (timeStamp.tv_sec < to->timeout.tv_sec)
            return;
        //如果时间到达的话,就将tcp的数据上传
        to->a_tcp->nids_state = NIDS_TIMED_OUT;
        //进行回调
        for(auto& func:tcptimeoutCallback_)
        {
            func(to->a_tcp,timeStamp);
        }
        next = to->next;
        nidsFreetcpstream(to->a_tcp);
    }
}

void TcpFragment::add2buf(struct halfStream * rcv, char *data, int datalen)
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

void TcpFragment::addFromskb(struct tcpStream *a_tcp, struct halfStream *rcv,
                             struct halfStream *snd,
                             u_char *data, int datalen,
                             u_int this_seq, char fin, char urg, u_int urg_ptr,timeval timeStamp)
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
        if (to_copy > 0)
        {
            add2buf(rcv, (char *)(data + lost), to_copy);
            notify(a_tcp, rcv,timeStamp);
        }
        rcv->urgdata = data[rcv->urg_ptr - this_seq];
        rcv->count_new_urg = 1;
        notify(a_tcp, rcv,timeStamp);
        rcv->count_new_urg = 0;
        rcv->urg_seen = 0;
        rcv->urg_count++;
        to_copy2 = this_seq + datalen - rcv->urg_ptr - 1;
        if (to_copy2 > 0)
        {
            add2buf(rcv, (char *)(data + lost + to_copy + 1), to_copy2);
            notify(a_tcp, rcv,timeStamp);
        }
    }
    else
    {
        if (datalen - lost > 0)
        {
            add2buf(rcv, (char *)(data + lost), datalen - lost);
            notify(a_tcp, rcv,timeStamp);
        }
    }
    if (fin)
    {
        snd->state = FIN_SENT;
        if (rcv->state == TCP_CLOSING)
            addTcpclosingtimeout(a_tcp,timeStamp);
    }
}

void TcpFragment::tcpQueue(struct tcpStream *a_tcp, struct tcphdr *this_tcphdr,
                           struct halfStream *snd, struct halfStream *rcv,
                           char *data, int datalen, int skblen,timeval timeStamp)
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
            getTs(this_tcphdr, &snd->curr_ts);
            //ok，更新集齐的数据区，值得一提的是add_from_skb函数一旦发现集齐了一段数据之后
            //便立刻调用notify函数，在notify函数里面将数据推给回调方
            addFromskb(a_tcp, rcv, snd, (u_char *) data, datalen, this_seq,
                       (this_tcphdr->th_flags & TH_FIN),
                       (this_tcphdr->th_flags & TH_URG),
                       ntohs(this_tcphdr->th_urp) + this_seq - 1,timeStamp);
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
                    addFromskb(a_tcp, rcv, snd, (u_char *) pakiet->data,
                               pakiet->len, pakiet->seq, pakiet->fin, pakiet->urg,
                               pakiet->urg_ptr + pakiet->seq - 1,timeStamp);
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
            LOG_WARN<<"no memory for tcpQueue";
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
                addTcpclosingtimeout(a_tcp,timeStamp);
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


int TcpFragment::getTs(struct tcphdr *this_tcphdr, unsigned int *ts)
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

int TcpFragment::getWscale(struct tcphdr *this_tcphdr, unsigned int *ws)
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

void TcpFragment::pruneQueue(struct halfStream *rcv, struct tcphdr *this_tcphdr)
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

void TcpFragment::handleAck(struct halfStream *snd, u_int acknum)
{
    int ackdiff;

    ackdiff = acknum - snd->ack_seq;
    if (ackdiff > 0)
    {
        snd->ack_seq = acknum;
    }
}

void TcpFragment::addNewtcp(struct tcphdr *this_tcphdr, struct ip *this_iphdr,timeval timeStamp)
{
    tcpStream *tolink;
    tcpStream *a_tcp;
    int hash_index;
    tuple4 addr;

    addr.source = ntohs(this_tcphdr->th_sport);
    addr.dest = ntohs(this_tcphdr->th_dport);
    addr.saddr = this_iphdr->ip_src.s_addr;
    addr.daddr = this_iphdr->ip_dst.s_addr;
    hash_index = hash.get_key(addr.saddr,addr.source,addr.daddr,addr.dest,tcpStreamTableSize);

    if (tcpNum > maxStream)
    {
        int orig_client_state=tcpOldest->client.state;
        tcpOldest->nids_state = NIDS_TIMED_OUT;
        LOG_WARN<<"the tcp_queue is out of range";
        nidsFreetcpstream(tcpOldest);
        if (orig_client_state!=TCP_SYN_SENT)
        {
            LOG_DEBUG<<"the tcp state is wrong";
        }
    }

    a_tcp = freeStreams;
    if (!a_tcp)
    {
        LOG_WARN<<"the a_tcp is wrong";
        pause();
    }
    freeStreams = a_tcp->next_free;

    tcpNum++;
    tolink = tcpStreamTable[hash_index];
    memset(a_tcp, 0, sizeof(struct tcpStream));
    a_tcp->hash_index = hash_index;
    a_tcp->addr = addr;
    a_tcp->client.state = TCP_SYN_SENT;
    a_tcp->client.seq = ntohl(this_tcphdr->th_seq) + 1;
    a_tcp->client.first_data_seq = a_tcp->client.seq;
    a_tcp->client.window = ntohs(this_tcphdr->th_win);
    a_tcp->client.ts_on = getTs(this_tcphdr, &a_tcp->client.curr_ts);
    a_tcp->client.wscale_on = getWscale(this_tcphdr, &a_tcp->client.wscale);
    a_tcp->server.state = TCP_CLOSE;
    a_tcp->next_node = tolink;
    a_tcp->prev_node = 0;
    a_tcp->ts = timeStamp.tv_sec;
    if (tolink)
        tolink->prev_node = a_tcp;
    tcpStreamTable[hash_index] = a_tcp;
    a_tcp->next_time = tcpLatest;
    a_tcp->prev_time = 0;
    if (!tcpOldest)
        tcpOldest = a_tcp;
    if (tcpLatest)
        tcpLatest->prev_time = a_tcp;
    tcpLatest = a_tcp;
}


//查询hash表查看是否已经存在tcp流
tcpStream * TcpFragment::findStream(tcphdr *this_tcphdr, ip *this_iphdr, int *from_client)
{
    tuple4 this_addr, reversed;
    tcpStream *a_tcp;

    this_addr.source = ntohs(this_tcphdr->th_sport);
    this_addr.dest = ntohs(this_tcphdr->th_dport);
    this_addr.saddr = this_iphdr->ip_src.s_addr;
    this_addr.daddr = this_iphdr->ip_dst.s_addr;
    a_tcp = nidsFindtcpStream(&this_addr);
    if (a_tcp)
    {
        *from_client = 1;
        return a_tcp;
    }
    reversed.source = ntohs(this_tcphdr->th_dport);
    reversed.dest = ntohs(this_tcphdr->th_sport);
    reversed.saddr = this_iphdr->ip_dst.s_addr;
    reversed.daddr = this_iphdr->ip_src.s_addr;
    a_tcp = nidsFindtcpStream(&reversed);
    if (a_tcp)
    {
        *from_client = 0;
        return a_tcp;
    }
    return 0;
}

tcpStream * TcpFragment::nidsFindtcpStream(tuple4 *addr)
{
    int hash_index;
    struct tcpStream *a_tcp;

    hash_index = hash.get_key(addr->saddr,addr->source,addr->daddr,addr->dest,tcpStreamTableSize);
    for (a_tcp = tcpStreamTable[hash_index];
         a_tcp && memcmp(&a_tcp->addr, addr, sizeof (struct tuple4));
         a_tcp = a_tcp->next_node);
    return a_tcp ? a_tcp : 0;
}

void TcpFragment::processTcp(ip *data, int skblen,timeval timeStamp)//传入数据与其长度
{
    tcpChecktimeouts(timeStamp);
    ip *this_iphdr = data;//ip与tcp结构体见后面说明

    tcphdr *this_tcphdr = (struct tcphdr *)((u_char*)data + 4 * this_iphdr->ip_hl);
    //计算ip部分偏移指到TCP头部
    int datalen, iplen;//数据部分长度，以及ip长度
    int from_client = 1;
    unsigned int tmp_ts;//时间戳
    tcpStream *a_tcp;
    halfStream *snd, *rcv;//一个方向上的TCP流，TCP分为两个方向上的，一个是客户到服务端，一个是服务端到客户

    uglyIphdr = this_iphdr;
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
//        LOG_WARN<<"the length of data is less than 0";//指示的长度与实际的不相符，指出错误
        return;
    } //数据部分小于0，发生错误，返回

    if ((this_iphdr->ip_src.s_addr | this_iphdr->ip_dst.s_addr) == 0)
    {
        LOG_WARN<<"the ip address is invalid";//指示的长度与实际的不相符，指出错误
        return;
    }
    //经过以上处，初步判断tcp包正常，进行入队操作，插入队列前，先进行此包的状态判断，判断此数据包处于何种状态
    if (!(a_tcp = findStream(this_tcphdr, this_iphdr, &from_client)))
    {
        /*是三次握手的第一个包*/
        /*tcp里流不存在时:且tcp数据包里的(syn=1 && ack==0 && rst==0)时,添加一条tcp流*/
        /*tcp第一次握手*/
        if ((this_tcphdr->th_flags & TH_SYN) &&
            !(this_tcphdr->th_flags & TH_ACK) &&
            !(this_tcphdr->th_flags & TH_RST))
            addNewtcp(this_tcphdr, this_iphdr,timeStamp);//发现新的TCP流，进行添加。
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

            if ((a_tcp->ts + 1800 < timeStamp.tv_sec))
            {

                if (!(this_tcphdr->th_flags & TH_ACK) &&
                    !(this_tcphdr->th_flags & TH_RST))
                {
                    // cleanup previous
                    nidsFreetcpstream(a_tcp);
                    // start new
                    addNewtcp(this_tcphdr, this_iphdr,timeStamp);
                }
            }
            return;
        }
        if (a_tcp->client.state != TCP_SYN_SENT ||
            a_tcp->server.state != TCP_CLOSE || !(this_tcphdr->th_flags & TH_ACK))
            return;
        if (a_tcp->client.seq != ntohl(this_tcphdr->th_ack))
            return;
        a_tcp->ts = timeStamp.tv_sec;
        a_tcp->server.state = TCP_SYN_RECV;
        a_tcp->server.seq = ntohl(this_tcphdr->th_seq) + 1;
        a_tcp->server.first_data_seq = a_tcp->server.seq;
        a_tcp->server.ack_seq = ntohl(this_tcphdr->th_ack);
        a_tcp->server.window = ntohs(this_tcphdr->th_win);
        if (a_tcp->client.ts_on)
        {
            a_tcp->server.ts_on = getTs(this_tcphdr, &a_tcp->server.curr_ts);
            if (!a_tcp->server.ts_on)
                a_tcp->client.ts_on = 0;
        }
        else a_tcp->server.ts_on = 0;
        if (a_tcp->client.wscale_on)
        {
            a_tcp->server.wscale_on = getWscale(this_tcphdr, &a_tcp->server.wscale);
            if (!a_tcp->server.wscale_on)
            {
                a_tcp->client.wscale_on = 0;
                a_tcp->client.wscale  = 1;
                a_tcp->server.wscale = 1;
            }
        }
        else
        {
            a_tcp->server.wscale_on = 0;
            a_tcp->server.wscale = 1;
        }
        return;
    }

    if (
            ! (!datalen && ntohl(this_tcphdr->th_seq) == rcv->ack_seq  )
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
            a_tcp->nids_state = NIDS_RESET;
            for(auto& func:tcprstCallback_)
            {
                func(a_tcp,timeStamp);
            }
        }
        nidsFreetcpstream(a_tcp);
        return;
    }

    /* PAWS check */
    /* PAWS(防止重复报文)check 检查时间戳*/
    if (rcv->ts_on && getTs(this_tcphdr, &tmp_ts) &&
        before(tmp_ts, snd->curr_ts))
        return;

/**********************************************************************
                        第三次握手包

***********************************************************************/

    /*
    从client --> server的包
     是从三次握手的第三个包分析开始的，进行一部分数据分析，和初始化
     连接状态
    */
    if ((this_tcphdr->th_flags & TH_ACK))//如果是从客户端来的，且两边都在第二次握手的状态上
    {
        if (from_client && a_tcp->client.state == TCP_SYN_SENT &&
            a_tcp->server.state == TCP_SYN_RECV)
        {

            //在此情况下，流水号又对得上，好的，这个包是第三次握手包，连接建立成功
            if (ntohl(this_tcphdr->th_ack) == a_tcp->server.seq)
            {
                a_tcp->client.state = TCP_ESTABLISHED;
                //更新客户端状态
                a_tcp->client.ack_seq = ntohl(this_tcphdr->th_ack);
                //更新ack序号
                a_tcp->ts = timeStamp.tv_sec;
                a_tcp->server.state = TCP_ESTABLISHED;
                a_tcp->nids_state = NIDS_JUST_EST;
                LOG_INFO<<"start2";
                for(auto& func:tcpconnectionCallback_)
                {
                    func(a_tcp,timeStamp);
                }
                a_tcp->nids_state = NIDS_DATA;
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
        handleAck(snd, ntohl(this_tcphdr->th_ack));
        if (rcv->state == FIN_SENT)
            rcv->state = FIN_CONFIRMED;
        if (rcv->state == FIN_CONFIRMED && snd->state == FIN_CONFIRMED)
        {
            a_tcp->nids_state = NIDS_CLOSE;
            LOG_INFO<<"close";
            for (auto& func:tcpcloseCallbacks_)
            {
                func(a_tcp,timeStamp);
            }
            nidsFreetcpstream(a_tcp);
            return;
        }
    }
    if (datalen + (this_tcphdr->th_flags & TH_FIN) > 0)
        tcpQueue(a_tcp, this_tcphdr, snd, rcv,
                 (char *) (this_tcphdr) + 4 * this_tcphdr->th_off,
                 datalen, skblen,timeStamp);
    snd->window = ntohs(this_tcphdr->th_win);
    if (rcv->rmem_alloc > 65535)
        pruneQueue(rcv, this_tcphdr);
}

void TcpFragment::notify(struct tcpStream * a_tcp, struct halfStream * rcv,timeval timeStamp)
{
    struct lurker_node *i, **prev_addr;
    char mask;
    LOG_INFO<<"notify";
    if (rcv->count_new_urg)
    {
        for(auto& func:tcpdataCallback_)
        {
            func(a_tcp,timeStamp);
        }
        goto prune_listeners;
    }

    do
    {
        int total;
        a_tcp->read = rcv->count - rcv->offset;
        total=a_tcp->read;
        for(auto& func:tcpdataCallback_)
        {
            func(a_tcp,timeStamp);
        }
        if (a_tcp->read>total-rcv->count_new)
            rcv->count_new=total-a_tcp->read;

        if (a_tcp->read > 0)
        {
            memmove(rcv->data, rcv->data + a_tcp->read, rcv->count - rcv->offset - a_tcp->read);
            rcv->offset += a_tcp->read;
        }
    }while (a_tcp->read>0 && rcv->count_new);
// we know that if one_loop_less!=0, we have only one callback to notify
    rcv->count_new=0;
    prune_listeners:
    return;
}

