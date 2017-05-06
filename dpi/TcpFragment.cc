//
// Created by jyc on 17-4-18.
//
#include "TcpFragment.h"
#include "Util.h"
#include <netinet/tcp.h>
#include <muduo/base/Logging.h>
TcpFragment::TcpFragment()
{
    LOG_INFO << "TcpFragment: started";
    int num_tcp_stream = 300001;
    tcpInit(num_tcp_stream);
}
TcpFragment::~TcpFragment()
{
    tcpExit();
}
int TcpFragment::tcpInit(int size)
{
    if (!size)
        return 0;

    //清空原来的所有定时器
    tcpStreamTableSize_ = (size_t) size;
    tcphashmap_.clear();
    finTimeoutSet_.clear();
    tcpTimeoutSet_.clear();
    return 0;
}

void TcpFragment::tcpExit(void)
{
    if(tcphashmap_.empty())
        return;
    for (auto It = tcphashmap_.begin(); It!=tcphashmap_.end();It++)
    {
        freeTcpData(&It->second);
    }
    tcphashmap_.clear();
    finTimeoutSet_.clear();
    tcpTimeoutSet_.clear();
}

void TcpFragment::tcpChecktimeouts(timeval timeStamp)
{
    while (!tcpTimeoutSet_.empty()) {

        auto it = tcpTimeoutSet_.begin();

        if (timeStamp.tv_sec < it->time.tv_sec) {
            break;
        }

        // TODO: neccesarry?
        if (it->a_tcp->isconnnection == 1) {
            for (auto &func : tcptimeoutCallback_) {
                assert(it->a_tcp != NULL);
                func(it->a_tcp, timeStamp);
            }
        }
        freeTcpstream(it->a_tcp);
    }

    while (!finTimeoutSet_.empty()) {

        auto it = finTimeoutSet_.begin();

        if (timeStamp.tv_sec < it->time.tv_sec) {
            break;
        }
        // time out
        for (auto &func : tcpcloseCallbacks_) {
            assert(it->a_tcp != NULL);
            func(it->a_tcp, timeStamp);
        }
        freeTcpstream(it->a_tcp);
    }

    /*
    for (auto It = tcpTimeoutSet_.begin(); It!=tcpTimeoutSet_.end(); It++)
    {
        if (timeStamp.tv_sec < It->time.tv_sec)
            break;

//        如果时间到达的话,就将tcp的数据上传
        if(It->a_tcp->isconnnection == 1)
        {
            for(auto& func:tcptimeoutCallback_)
            {
                if(It->a_tcp==NULL)
                    break;
                func(It->a_tcp, timeStamp);
            }
        }
        freeTcpstream(It->a_tcp);
    }

    for (auto It = finTimeoutSet_.begin();It!= finTimeoutSet_.end();It++)
    {
        if (timeStamp.tv_sec < It->time.tv_sec)
            break;
        //如果时间到达的话,就将tcp的数据上传
        //进行回调
        freeTcpstream(It->a_tcp);
    }
     */
    return;
}

void TcpFragment::processTcp(ip * data,int skblen, timeval timeStamp)
{
    tcpChecktimeouts(timeStamp);

    ip* this_iphdr = data;
    tcphdr *this_tcphdr = (tcphdr *)((u_char*)data + 4 * this_iphdr->ip_hl);
    int datalen,iplen;
    int from_client = 1;
    TcpStream* a_tcp;
    HalfStream* snd, *rcv;//一个方向上的TCP流，TCP分为两个方向上的，一个是客户到服务端，一个是服务端到客户
    iplen = ntohs(this_iphdr->ip_len);
    if ((unsigned)iplen < 4 * this_iphdr->ip_hl + sizeof(struct tcphdr))
    {
        LOG_WARN<<"the length of iphdr is wrong";//指示的长度与实际的不相符，指出错误
        return;
    }
    datalen = iplen - 4 * this_iphdr->ip_hl - 4 * this_tcphdr->th_off;
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

    /***经过以上处，初步判断tcp包正常，进行入队操作，插入队列前，先进行此包的状态判断，判断此数据包处于何种状态***/


    //在哈希表里找找，如果没有此tcp会话则看看是不是要新建一个
    if ((a_tcp = findStream(this_tcphdr, this_iphdr, &from_client)) == NULL)
    {
        /*是三次握手的第一个包*/
        /*tcp里流不存在时:且tcp数据包里的(syn=1 && ack==0 && rst==0)时,添加一条tcp流*/
        /*tcp第一次握手*/
        if ((this_tcphdr->th_flags & TH_SYN) &&
            !(this_tcphdr->th_flags & TH_ACK) &&
            !(this_tcphdr->th_flags & TH_RST)) {

            addNewtcp(this_tcphdr, this_iphdr, timeStamp);//发现新的TCP流，进行添加。
        }
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

/*****************************************************************
                三次握手的第二次握手
******************************************************************/
    /*tcp 三次握手， SYN ==1，ACK==1,tcp第二次握手(server -> client的同步响应)*/
//来了一个SYN包
    if ((this_tcphdr->th_flags & TH_SYN))
    {
        //syn包是用来建立新连接的，所以，要么来自客户端且没标志（前面处理了），要么来自服务端且加ACK标志
        //所以这里只能来自服务器，检查服务器状态是否正常，不正常的话果断忽略这个包
        if (from_client)
        {
            // 保活计时器,半个小时
            if ((a_tcp->ts + 1800 < timeStamp.tv_sec))
            {

                if (!(this_tcphdr->th_flags & TH_ACK) &&
                    !(this_tcphdr->th_flags & TH_RST))
                {
                    // cleanup previous
                    freeTcpstream(a_tcp);
                    // start new
                    addNewtcp(this_tcphdr, this_iphdr,timeStamp);
                }
            }
            return;
        }
        //如果是来自server端的,则说明正常,然后查看状态
        if (a_tcp->client.state != TCP_SYN_SENT ||
            a_tcp->server.state != TCP_CLOSE || !(this_tcphdr->th_flags & TH_ACK))
            return;
        //检查序号是否匹配
        if (a_tcp->client.seq != ntohl(this_tcphdr->th_ack))
            return;
        //更新server端的状态
        a_tcp->ts = timeStamp.tv_sec;
        a_tcp->server.state = TCP_SYN_RECV;
        a_tcp->server.seq = ntohl(this_tcphdr->th_seq) + 1;
        a_tcp->server.first_data_seq = a_tcp->server.seq;
        a_tcp->server.ack_seq = ntohl(this_tcphdr->th_ack);
        a_tcp->server.window = ntohs(this_tcphdr->th_win);
        //再是窗口扩大选项
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
        //syn包处理完，返回
        return;
    }
//    LOG_INFO<<"THIS WAY:";
//
    if (!(!datalen && ntohl(this_tcphdr->th_seq) == rcv->ack_seq ) //不是流水号正确且没数据的包
        &&//而且这个包不再当前窗口之内
            (!before(ntohl(this_tcphdr->th_seq), rcv->ack_seq + rcv->window*rcv->wscale) ||
              before(ntohl(this_tcphdr->th_seq) + datalen, rcv->ack_seq)))
        return;

    //如果是rst包，ok，关闭连接
    //将现有数据推给注册的回调方，然后销毁这个会话。
    if ((this_tcphdr->th_flags & TH_RST))
    {
        if(a_tcp->isconnnection==1)
        {
            for(auto& func:tcprstCallback_)
            {
                func(a_tcp,timeStamp);
            }
        }
        freeTcpstream(a_tcp);
        return;
    }

    //ACK报文来了
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
                for(auto& func:tcpconnectionCallback_)
                {
                    func(a_tcp,timeStamp);
                }
                a_tcp->isconnnection = 1;
            }
        }
    }
    //自此，握手包处理完毕

    /*************************************************************
                            挥手过程
     **************************************************************/
/*数据结束的包的判断*/

    if ((this_tcphdr->th_flags & TH_ACK))
    {
        //先调用handle_ack更新ack序号
        handleAck(snd, ntohl(this_tcphdr->th_ack));
        if (rcv->state == FIN_SENT)
            rcv->state = FIN_CONFIRMED;
        //流关闭，并进行回调
        if (rcv->state == FIN_CONFIRMED && snd->state == FIN_CONFIRMED)
        {
            if(a_tcp->isconnnection==1)
            {
                for (auto& func:tcpcloseCallbacks_)
                {
                    func(a_tcp,timeStamp);
                }
            }
            freeTcpstream(a_tcp);
            return;
        }
    }
//    if(a_tcp->addr.dest == 80&&datalen>0)
//    {
//        LOG_INFO<<"80 data";
//    }
    //下面处理数据包，和初始的fin包
    if (datalen + (this_tcphdr->th_flags & TH_FIN) > 0)
    {
        //就将数据传送出去
        tcpQueue(a_tcp, this_tcphdr, snd, rcv, (char *) (this_tcphdr) + 4 * this_tcphdr->th_off,
                  datalen, skblen,timeStamp);
    }

    //更新窗口大小
    snd->window = ntohs(this_tcphdr->th_win);
    if (rcv->rmem_alloc > 65535)
    {
        freeTcpstream(a_tcp);
    }
}

void TcpFragment::freeTcpData(TcpStream *a_tcp)
{
    delTcptimeout(a_tcp);
    delFintimeout(a_tcp);
    purgeQueue(&a_tcp->client);
    purgeQueue(&a_tcp->server);
}

void TcpFragment::freeTcpstream(TcpStream *a_tcp)
{
    if(a_tcp==NULL)
        return;
    int hash_index = a_tcp->hash_index;
    delTcptimeout(a_tcp);
    delFintimeout(a_tcp);
    purgeQueue(&a_tcp->client);
    purgeQueue(&a_tcp->server);
    auto range = tcphashmap_.equal_range(hash_index);
    for (auto it = range.first; it != range.second;)
    {
        if(&it->second == a_tcp)
        {
            it = tcphashmap_.erase(it);
        }
        else
        {
            ++it;
        }
    }

}


TcpStream* TcpFragment::findStream_aux(tuple4 addr)
{
    int hash_index;
    TcpStream* a_tcp = NULL;
    hash_index = hash.get_key(addr.saddr,addr.source,addr.daddr,addr.dest,tcpStreamTableSize_);
    auto I = tcphashmap_.find(hash_index);
    if(I==tcphashmap_.end())
    {
        return 0;
    }
    else
    {
        auto range = tcphashmap_.equal_range(hash_index);
        for (auto it = range.first; it != range.second; ++it)
        {
            if(it->second.addr == addr)
            {
                a_tcp = &(it->second);
            }
        }
        return a_tcp?a_tcp:0;
    }
}


TcpStream* TcpFragment::findStream(tcphdr *this_tcphdr, ip *this_iphdr, int *from_client)
{
     //客户端和服务器都表示同一个hash
     tuple4 this_addr,reversed;
     this_addr.source = ntohs(this_tcphdr->th_sport);
     this_addr.dest = ntohs(this_tcphdr->th_dport);
     this_addr.saddr = this_iphdr->ip_src.s_addr;
     this_addr.daddr = this_iphdr->ip_dst.s_addr;
     TcpStream* a_tcp = findStream_aux(this_addr);
     if(a_tcp)
     {
         *from_client = 1;
         return a_tcp;
     }

     reversed.source = ntohs(this_tcphdr->th_dport);
     reversed.dest = ntohs(this_tcphdr->th_sport);
     reversed.saddr = this_iphdr->ip_dst.s_addr;
     reversed.daddr = this_iphdr->ip_src.s_addr;
     a_tcp = findStream_aux(reversed);
     if (a_tcp)
     {
         *from_client = 0;
         return a_tcp;
     }
     return 0;
}

void TcpFragment::addNewtcp(tcphdr *this_tcphdr,ip *this_iphdr,timeval timeStamp)
{
    int hash_index;
    TcpStream a_tcp;
    tuple4 addr;

    addr.source = ntohs(this_tcphdr->th_sport);
    addr.dest = ntohs(this_tcphdr->th_dport);
    addr.saddr = this_iphdr->ip_src.s_addr;
    addr.daddr = this_iphdr->ip_dst.s_addr;
    hash_index = hash.get_key(addr.saddr,addr.source,addr.daddr,addr.dest,tcpStreamTableSize_);

    //队列已经满了,新的直接不缓存
    if (tcphashmap_.size()> tcpStreamTableSize_)
    {
        LOG_WARN<<"the tcp_queue is out of range";
        return;
    }

    a_tcp.client.count = 0;
    a_tcp.server.count = 0;
    a_tcp.isconnnection = 0;
    a_tcp.server.rmem_alloc = 0;
    a_tcp.client.rmem_alloc = 0;
    a_tcp.client.ack_seq = 0;
    a_tcp.server.ack_seq = 0;
    a_tcp.client.urg_count = 0;
    a_tcp.server.urg_count = 0;
    a_tcp.hash_index = hash_index;
    a_tcp.addr = addr;
    a_tcp.client.state = TCP_SYN_SENT;
    //发送后序号+1
    a_tcp.client.seq = ntohl(this_tcphdr->th_seq) + 1;
    a_tcp.client.first_data_seq = a_tcp.client.seq;
    a_tcp.client.window = ntohs(this_tcphdr->th_win);
    a_tcp.client.wscale_on = getWscale(this_tcphdr, &a_tcp.client.wscale);
    a_tcp.server.state = TCP_CLOSE;
    a_tcp.ts = timeStamp.tv_sec;
    tcphashmap_.insert(std::make_pair(hash_index,a_tcp));
    // LOG_INFO<<"the hashsize:"<<tcphashmap_.size();

    tuple4 this_addr;
    this_addr.source = ntohs(this_tcphdr->th_sport);
    this_addr.dest = ntohs(this_tcphdr->th_dport);
    this_addr.saddr = this_iphdr->ip_src.s_addr;
    this_addr.daddr = this_iphdr->ip_dst.s_addr;
    TcpStream* temp = findStream_aux(this_addr);
    if(temp)
        addTcptimeout(temp,timeStamp);

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

void TcpFragment::handleAck(HalfStream *snd, u_int acknum)
{
    int ackdiff;

    ackdiff = acknum - snd->ack_seq;
    if (ackdiff > 0)
    {
        snd->ack_seq = acknum;
    }
}

void TcpFragment::tcpQueue(TcpStream *a_tcp, tcphdr *this_tcphdr,
                           HalfStream *snd, HalfStream *rcv,
                           char *data, int datalen, int skblen,timeval timeStamp)
{
    u_int this_seq = ntohl(this_tcphdr->th_seq);

    //EXP_SEQ是目前已集齐的数据流水号，我们希望收到从这里开始的数据
    //先判断数据是不是在EXP_SEQ之前开始
    if (!after(this_seq, (snd->first_data_seq + rcv->count + rcv->urg_count)))
    {
        //再判断数据长度是不是在EXP_SEQ之后，如果是，说明有新数据，否则是重发的包，无视之
//        LOG_INFO<<(u_int)(this_seq + datalen + (this_tcphdr->th_flags & TH_FIN))<<" "<<(u_int)(snd->first_data_seq) <<" "<< rcv->count <<" "<<rcv->urg_count;
        if (after(this_seq + datalen + (this_tcphdr->th_flags & TH_FIN), (snd->first_data_seq + rcv->count + rcv->urg_count)))
        {
            //ok，更新集齐的数据区，值得一提的是add_from_skb函数一旦发现集齐了一段数据之后
            //便立刻调用notify函数，在notify函数里面将数据推给回调方
            updateTcptimeout(a_tcp, timeStamp);
            addFromskb(a_tcp, rcv, snd, (u_char *) data, datalen, this_seq,
                       (this_tcphdr->th_flags & TH_FIN),
                       (this_tcphdr->th_flags & TH_URG),
                       ntohs(this_tcphdr->th_urp) + this_seq - 1, timeStamp);
            /*
             * Do we have any old packets to ack that the above
             * made visible? (Go forward from skb)
             */
            //此时EXP_SEQ有了变化了，看看缓冲区里的包有没有符合条件能用同样的方法处理掉的
            //有就处理掉，然后释放
            if(rcv->fraglist.empty())
                return;
            for (auto frag = rcv->fraglist.begin(); frag != rcv->fraglist.end();)
            {
                if (after(frag->seq, EXP_SEQ)) //流水号在后面
                    break;
                if (after(frag->seq + frag->len + frag->fin, EXP_SEQ))
                {
                    updateTcptimeout(a_tcp, timeStamp);
                    addFromskb(a_tcp, rcv, snd, (u_char *) frag->data,
                               frag->len, frag->seq, frag->fin, frag->urg,
                               frag->urg_ptr + frag->seq - 1, timeStamp);
                }
                rcv->rmem_alloc -= frag->truesize;
                free(frag->data);
                frag = rcv->fraglist.erase(frag);
            }
        }
        else
            return;
    }
    //这里说明现在这个包是个乱序到达的（数据开始点超过了EXP_SEQ），放到缓冲区等待处理
    else
    {
        Skbuff pakiet;

        pakiet.truesize = skblen;
        rcv->rmem_alloc += pakiet.truesize;
        pakiet.len = datalen;
        pakiet.data = malloc(datalen);
        if (!pakiet.data)
        {
            LOG_WARN<<"no memory for a frag data";
        }
        memmove(pakiet.data, data, datalen);
        pakiet.fin = (this_tcphdr->th_flags & TH_FIN);
        /* Some Cisco - at least - hardware accept to close a TCP connection
         * even though packets were lost before the first TCP FIN packet and
         * never retransmitted; this violates RFC 793, but since it really
         * happens, it has to be dealt with... The idea is to introduce a 10s
         * timeout after TCP FIN packets were sent by both sides so that
         * corresponding libnids resources can be released instead of waiting
         * for retransmissions which will never happen.  -- Sebastien Raveau
         */
        if (pakiet.fin)
        {
            snd->state = TCP_CLOSING;
            if (rcv->state == FIN_SENT || rcv->state == FIN_CONFIRMED)
                addFintimeout(a_tcp,timeStamp);
        }
        pakiet.seq = this_seq;
        pakiet.urg = (this_tcphdr->th_flags & TH_URG);
        pakiet.urg_ptr = ntohs(this_tcphdr->th_urp);
        auto It = rcv->fraglist.begin();
        while(It!=rcv->fraglist.end())
        {
            if (after(It->seq, this_seq))
            {
                break;
            }
            It++;
        }
        if(It == rcv->fraglist.begin())
        {
            rcv->fraglist.push_front(pakiet);
            return;
        }
        if(It!=rcv->fraglist.end())
            It--;
        else
            rcv->fraglist.push_back(pakiet);
    }
}

void TcpFragment::addFromskb(TcpStream *a_tcp, HalfStream *rcv, HalfStream *snd,
                             u_char *data, int datalen,
                             u_int this_seq, char fin, char urg, u_int urg_ptr,timeval timeStamp)
{
    u_int lost = EXP_SEQ - this_seq;
    int to_copy, to_copy2;
    //如果有紧急数据在当前期望序号之后
    if (urg && after(urg_ptr, EXP_SEQ - 1) &&
        (!rcv->urg_seen || after(urg_ptr, rcv->urg_ptr)))
    {
        rcv->urg_ptr = urg_ptr;
        rcv->urg_seen = 1;
    }

    //处理紧急数据
    if (rcv->urg_seen && after(rcv->urg_ptr + 1, this_seq + lost) && before(rcv->urg_ptr, this_seq + datalen))
    {
        to_copy = rcv->urg_ptr - (this_seq + lost);
        if (to_copy > 0)
        {
            rcv->count_new = to_copy;
            rcv->count += to_copy;
            notify(a_tcp, rcv,timeStamp,data+lost,to_copy);
        }
        rcv->urgdata = data[rcv->urg_ptr - this_seq];
        rcv->count_new_urg = 1;
        notify(a_tcp, rcv,timeStamp,data,datalen);
        rcv->count_new_urg = 0;
        rcv->urg_seen = 0;
        rcv->urg_count++;
        to_copy2 = this_seq + datalen - rcv->urg_ptr - 1;
        if (to_copy2 > 0)
        {
            rcv->count_new = to_copy2;
            rcv->count += to_copy2;
            notify(a_tcp, rcv,timeStamp,(data + lost + to_copy + 1),to_copy2);
        }
    }
    else
    {
        //不是紧急数据,但是有有效的数据
        if (datalen - lost > 0)
        {
            rcv->count_new = datalen - lost;
            rcv->count += datalen - lost;
            notify(a_tcp, rcv,timeStamp,(data+lost),datalen-lost);
        }
    }
    if (fin)
    {
        snd->state = FIN_SENT;
        if (rcv->state == TCP_CLOSING)
            addFintimeout(a_tcp,timeStamp);
    }
}

void TcpFragment::notify(TcpStream * a_tcp, HalfStream * rcv,timeval timeStamp,u_char* data,int datalen)
{
    if(a_tcp==NULL)
        return;
    if (rcv->count_new_urg)
    {
        if(data&&a_tcp->isconnnection)
        {
            for(auto& func:tcpdataCallback_)
            {
                if(rcv == &a_tcp->server)
                    func(a_tcp,timeStamp,data,datalen,FROMCLIENT);
                else
                    func(a_tcp,timeStamp,data,datalen,FROMSERVER);
            }
        }

        return;
    }
    for(auto& func:tcpdataCallback_)
    {
        if(data&&a_tcp->isconnnection)
        {
            if (rcv == &a_tcp->server)
                func(a_tcp, timeStamp, data, datalen, FROMCLIENT);
            else
                func(a_tcp, timeStamp, data, datalen, FROMSERVER);
        }
    }
    // we know that if one_loop_less!=0, we have only one callback to notify
    rcv->count_new=0;
    return;
}

void TcpFragment::addTcptimeout(TcpStream *a_tcp,timeval timeStamp)
{
    Timeout temp;
    temp.a_tcp = a_tcp;
    temp.time = timeStamp;
    temp.time.tv_sec += 60;
    tcpTimeoutSet_.insert(std::move(temp));
}

void TcpFragment::delTcptimeout(TcpStream *a_tcp)
{

    for(auto It = tcpTimeoutSet_.begin();It!=tcpTimeoutSet_.end();)
    {
        if(It->a_tcp == a_tcp)
        {
            It = tcpTimeoutSet_.erase(It);
        }
        else
        {
            ++It;
        }
    }
}

void TcpFragment::addFintimeout(TcpStream *a_tcp,timeval timeStamp)
{
    Timeout temp;
    temp.a_tcp = a_tcp;
    temp.time = timeStamp;
    temp.time.tv_sec += 5;
    finTimeoutSet_.insert(std::move(temp));
}

void TcpFragment::delFintimeout(TcpStream *a_tcp)
{
    for(auto It = finTimeoutSet_.begin();It!=finTimeoutSet_.end();)
    {
        if(It->a_tcp == a_tcp)
        {
            It = finTimeoutSet_.erase(It);
        }
        else
        {
            ++It;
        }
    }
}

void TcpFragment::updateTcptimeout(TcpStream *a_tcp,timeval timeStamp)
{
    delTcptimeout(a_tcp);
    addTcptimeout(a_tcp,timeStamp);
}

void TcpFragment::purgeQueue(HalfStream *h)
{
    if(h->fraglist.size()<=0)
        return;
    for(auto res = h->fraglist.begin();res!=h->fraglist.end();res++)
    {
        if(res->data!=NULL)
            free(res->data);
    }
    h->rmem_alloc = 0;
    h->fraglist.clear();
}


