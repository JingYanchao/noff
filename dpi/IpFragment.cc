

//
// Created by jyc on 17-4-14.
//
#include "IpFragment.h"
#include <muduo/base/Logging.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

Ip_fragment::Ip_fragment()
{
    struct timeval tv;

    gettimeofday(&tv, 0);
    time0 = tv.tv_sec;
    fragtable = (struct hostfrags **) calloc(3001, sizeof(struct hostfrags *));
    if (!fragtable)
        LOG_ERROR<<"ip_frag_init";
    hash_size = 3001;
}

Ip_fragment::Ip_fragment(size_t n)
{
    struct timeval tv;

    gettimeofday(&tv, 0);
    time0 = tv.tv_sec;
    fragtable = (struct hostfrags **) calloc(n, sizeof(struct hostfrags *));
    if (!fragtable)
        LOG_ERROR<<"ip_frag_init";
    hash_size = n;
}

Ip_fragment::~Ip_fragment()
{
    if (fragtable)
    {
        free(fragtable);
        fragtable = NULL;
    }
}


int Ip_fragment::jiffies()
{
    struct timeval tv;
    if (timenow)
        return timenow;
    gettimeofday(&tv, 0);
    timenow = (int) ((tv.tv_sec - time0) * 1000 + tv.tv_usec / 1000);
    return timenow;
}

void Ip_fragment::startIpfragProc(ip *data, int len)
{
    struct proc_node *i;
    struct ip *iph = data;
    int need_free = 0;
    int skblen;

    if (len < (int)sizeof(struct ip) || iph->ip_hl < 5 || iph->ip_v != 4 ||
        len < ntohs(iph->ip_len) || ntohs(iph->ip_len) < iph->ip_hl << 2)
    {
        LOG_WARN<<"ip data is invalid:"<<iph->ip_id;
        return;
    }

    switch (ipDefragStub((struct ip *) data, &iph))
    {
        case IPF_ISF:
            return;
        case IPF_NOTF:
            need_free = 0;
            iph = (struct ip *) data;
            break;
        case IPF_NEW:
            need_free = 1;
            break;
        default:
            LOG_WARN<<"ip defrag_stub is invalid:"<<iph->ip_id;;
    }
    skblen = ntohs(iph->ip_len) + 16;
    if (!need_free)
        skblen += -1;
    skblen = (skblen + 15) & ~15;
    skblen += 168; //sk_buff_size;

    genIpProc(reinterpret_cast<u_char *>(iph), skblen);
    if (need_free)
        free(iph);
}

void Ip_fragment::genIpProc(u_char *data, int skblen)
{

    switch (((ip *) data)->ip_p)
    {
        case IPPROTO_TCP:
        {
            tcphdr *this_tcphdr = (tcphdr *)(data + 4*((ip *) data)->ip_p);
            for(auto& func:tcpCallbacks_)
            {
                func(this_tcphdr, skblen-4*((ip *) data)->ip_p);
            }
            break;
        }
        case IPPROTO_UDP:
        {
            udphdr *this_udphdr = (udphdr *)(data + 4*((ip *) data)->ip_p);
            for(auto& func:udpCallbacks_)
            {
                func(this_udphdr,skblen-4*((ip *) data)->ip_p);
            }
            break;
        }
        case IPPROTO_ICMP:
        {
            for(auto& func:icmpCallbacks_)
            {
                func(data);
            }
            break;
        }
        default:
            break;
    }
}

char* Ip_fragment::ipDefrag(struct ip *iph, struct sk_buff *skb)
{
    struct ipfrag *prev, *next, *tmp;
    struct ipfrag *tfp;
    struct ipq *qp;
    char *skb2;
    unsigned char *ptr;
    int flags, offset;
    int i, ihl, end;

    //如果是分片，而且host哈希表里还没有对应的host项的话，果断新建一个
    //此处还负责将this_host变量设为当前ip对应的host
    if (!hostfragFind(iph) && skb)
        hostfragCreate(iph);

    //内存用太多了，释放当前host分片所用的内存
    if (this_host)
        if (this_host->ip_frag_mem > IPFRAG_HIGH_THRESH)
            ipEvictor();

    //找到这个ip包对应的ip分片链表
    if (this_host)
        qp = ipFind(iph);
    else
        qp = 0;

    /* Is this a non-fragmented datagram? */
    offset = ntohs(iph->ip_off);
    flags = offset & ~IP_OFFSET;
    offset &= IP_OFFSET;
    if (((flags & IP_MF) == 0) && (offset == 0))
    {
        if (qp != NULL)
            ipFree(qp);		/* Fragmented frame replaced by full
				   unfragmented copy */
        return 0;
    }

    /* ipEvictor() could have removed all queues for the current host */
    if (!this_host)
        hostfragCreate(iph);

    offset <<= 3;			/* offset is in 8-byte chunks */
    ihl = iph->ip_hl * 4;

    /*
      If the queue already existed, keep restarting its timer as long as
      we still are receiving fragments.  Otherwise, create a fresh queue
      entry.
    */
    //如果当前host下来过此包的碎片,，就丢弃
    if (qp != NULL)
    {
        /* ANK. If the first fragment is received, we should remember the correct
           IP header (with options) */
        if (offset == 0)
        {
            qp->ihlen = ihl;
            memcpy(qp->iph, iph, ihl + 8);
        }
        delTimer(&qp->timer);
        qp->timer.expires = jiffies() + IP_FRAG_TIME;	/* about 30 seconds */
        qp->timer.data = (unsigned long) qp;	/* pointer to queue */
        addTimer(&qp->timer);
    }
    else//否则新建一个碎片队列
    {
        /* If we failed to create it, then discard the frame. */
        if ((qp = ipCreate(iph)) == NULL)
        {
            kfreeSkb(skb, FREE_READ);
            return NULL;
        }
    }
    /* Attempt to construct an oversize packet. */
    //再大的ip包也不能大过65535啊，一经发现，直接放弃
    if (ntohs(iph->ip_len) + (int) offset > 65535)
    {
        // NETDEBUG(printk("Oversized packet received from %s\n", int_ntoa(iph->ip_src.s_addr)));
        LOG_WARN<<"ip over size:"<<iph->ip_id;
        kfreeSkb(skb, FREE_READ);
        return NULL;
    }
    /* Determine the position of this fragment. */
    //如果有重叠，把重叠的旧的部分去掉
    end = offset + ntohs(iph->ip_len) - ihl;

    /* Point into the IP datagram 'data' part. */
    ptr = (unsigned char *)(skb->data + ihl);

    /* Is this the final fragment? */
    if ((flags & IP_MF) == 0)
        qp->len = end;

    /*
      Find out which fragments are in front and at the back of us in the
      chain of fragments so far.  We must know where to put this
      fragment, right?
    */
    prev = NULL;
    for (next = qp->fragments; next != NULL; next = next->next)
    {
        if (next->offset >= offset)
            break;			/* bingo! */
        prev = next;
    }
    /*
      We found where to put this one.  Check for overlap with preceding
      fragment, and, if needed, align things so that any overlaps are
      eliminated.
    */
    if (prev != NULL && offset < prev->end)
    {
        LOG_WARN<<"ip overlap:"<<iph->ip_id;
        i = prev->end - offset;
        offset += i;		/* ptr into datagram */
        ptr += i;			/* ptr into fragment data */
    }
    /*
      Look for overlap with succeeding segments.
      If we can merge fragments, do it.
    */
    for (tmp = next; tmp != NULL; tmp = tfp)
    {
        tfp = tmp->next;
        if (tmp->offset >= end)
            break;			/* no overlaps at all */
        LOG_WARN<<"ip overlap:"<<iph->ip_id;

        i = end - next->offset;	/* overlap is 'i' bytes */
        tmp->len -= i;		/* so reduce size of    */
        tmp->offset += i;		/* next fragment        */
        tmp->ptr += i;
        /*
          If we get a frag size of <= 0, remove it and the packet that it
          goes with. We never throw the new frag away, so the frag being
          dumped has always been charged for.
        */
        if (tmp->len <= 0)
        {
            if (tmp->prev != NULL)
                tmp->prev->next = tmp->next;
            else
                qp->fragments = tmp->next;

            if (tmp->next != NULL)
                tmp->next->prev = tmp->prev;

            next = tfp;		/* We have killed the original next frame */

            fragKfreeskb(tmp->skb, FREE_READ);
            fragKfrees(tmp, sizeof(struct ipfrag));
        }
    }

    /* Insert this fragment in the chain of fragments. */
    //下面往队列中插入当前碎片
    tfp = NULL;
    tfp = ip_frag_create(offset, end, skb, ptr);

    /*
      No memory to save the fragment - so throw the lot. If we failed
      the frag_create we haven't charged the queue.
    */
    if (!tfp)
    {
        LOG_ERROR<<"memory ipDefrag"<<iph->ip_id;
        kfreeSkb(skb, FREE_READ);
        return NULL;
    }
    /* From now on our buffer is charged to the queues. */
    tfp->prev = prev;
    tfp->next = next;
    if (prev != NULL)
        prev->next = tfp;
    else
        qp->fragments = tfp;

    if (next != NULL)
        next->prev = tfp;

    /*
      OK, so we inserted this new fragment into the chain.  Check if we
      now have a full IP datagram which we can bump up to the IP
      layer...
    */


    //查看是不是碎片都搜集齐了，如果齐了，组合成一个大ip包返回
    if (ipDone(qp))
    {
        skb2 = ipGlue(qp);		/* glue together the fragments */
        return (skb2);
    }
    return (NULL);
}



int Ip_fragment::ipDefragStub(struct ip *iph, struct ip **defrag)
{
    int offset, flags, tot_len;
    struct sk_buff *skb;

    numpack++;
    timenow = 0;
    while (timer_head && timer_head->expires < jiffies())
    {
        this_host = ((struct ipq *) (timer_head->data))->hf;
        ipExpire(timer_head->data);
    }
    offset = ntohs(iph->ip_off);
    flags = offset & ~IP_OFFSET;
    offset &= IP_OFFSET;
    //没有分片
    if (((flags & IP_MF) == 0) && (offset == 0))
    {
        ipDefrag(iph, 0);
        return IPF_NOTF;
    }

    //此包是分片，先申请一个sk_buff把分片的数据保存起来，然后交给defrag函数
    tot_len = ntohs(iph->ip_len);
    skb = (struct sk_buff *) malloc(tot_len + sizeof(struct sk_buff));
    if (!skb)
        LOG_ERROR<<"Out of memory in skb";
    skb->data = (char *) (skb + 1);
    memmove(skb->data, iph, tot_len);
    skb->truesize = tot_len + 16 - 1;
    skb->truesize = (skb->truesize + 15) & ~15;
    skb->truesize += 168;

    //如果集齐了一个ip包的所有分片ip_defrag将返回合并后的ip包，此时返回IPF_NEW，进行下一步的ip包处理
    //否则，返回IPF_ISF，跳过ip包处理
    if ((*defrag = (struct ip *) ipDefrag((struct ip *) (skb->data), skb)))
        return IPF_NEW;

    return IPF_ISF;
}

//根据所有分片拼接成一个完整的报文
char* Ip_fragment::ipGlue(struct ipq *qp)
{
    char *skb;
    struct ip *iph;
    struct ipfrag *fp;
    unsigned char *ptr;
    int count, len;

    /* Allocate a new buffer for the datagram. */
    len = qp->ihlen + qp->len;

    if (len > 65535)
    {
        // NETDEBUG(printk("Oversized IP packet from %s.\n", int_ntoa(qp->iph->ip_src.s_addr)));
        LOG_WARN<<"ip oversize"<<iph->ip_id;
        ipFree(qp);
        return NULL;
    }
    if ((skb = (char *) malloc(len)) == NULL)
    {
        // NETDEBUG(printk("IP: queue_glue: no memory for gluing queue %p\n", qp));
        LOG_ERROR<<"ipGlue";
        ipFree(qp);
        return (NULL);
    }
    /* Fill in the basic details. */
    ptr = (unsigned char *)skb;
    memmove(ptr, ((unsigned char *) qp->iph), qp->ihlen);
    ptr += qp->ihlen;
    count = 0;

    /* Copy the data portions of all fragments into the new buffer. */
    fp = qp->fragments;
    while (fp != NULL)
    {
        if (fp->len < 0 || fp->offset + qp->ihlen + fp->len > len)
        {
            //NETDEBUG(printk("Invalid fragment list: Fragment over size.\n"));
            LOG_WARN<<"ip_invlist"<<iph->ip_id;
            ipFree(qp);
            //kfreeSkb(skb, FREE_WRITE);
            //ip_statistics.IpReasmFails++;
            free(skb);
            return NULL;
        }
        memmove((ptr + fp->offset), fp->ptr, fp->len);
        count += fp->len;
        fp = fp->next;
    }
    /* We glued together all fragments, so remove the queue entry. */
    ipFree(qp);

    /* Done with all fragments. Fixup the new IP header. */
    iph = (struct ip *) skb;
    iph->ip_off = 0;
    iph->ip_len = htons((iph->ip_hl * 4) + count);
    // skb->ip_hdr = iph;

    return (skb);
}


//看一个分片序列是否重组完成
int Ip_fragment::ipDone(struct ipq *qp)
{
    struct ipfrag *fp;
    int offset;

    /* Only possible if we received the final fragment. */
    if (qp->len == 0)
        return (0);

    /* Check all fragment offsets to see if they connect. */
    fp = qp->fragments;
    offset = 0;
    while (fp != NULL)
    {
        if (fp->offset > offset)//新的碎片偏移量和上一个报文的偏移量不相等
            return (0);
        offset = fp->end;
        fp = fp->next;
    }
    /* All fragments are present. */
    return (1);
}


//创建一个新的ip重组队列
ipq* Ip_fragment::ipCreate(struct ip *iph)
{
    struct ipq *qp;
    int ihlen;

    qp = (struct ipq *) fragKmalloc(sizeof(struct ipq), GFP_ATOMIC);
    if (qp == NULL)
    {
        // NETDEBUG(printk("IP: create: no memory left !\n"));
        LOG_WARN<<"out of memory in ipCreate";
        return (NULL);
    }
    memset(qp, 0, sizeof(struct ipq));

    /* Allocate memory for the IP header (plus 8 octets for ICMP). */
    ihlen = iph->ip_hl * 4;
    qp->iph = (struct ip *) fragKmalloc(64 + 8, GFP_ATOMIC);
    if (qp->iph == NULL)
    {
        //NETDEBUG(printk("IP: create: no memory left !\n"));
        LOG_WARN<<"out of memory in ipCreate";
        fragKfrees(qp, sizeof(struct ipq));
        return (NULL);
    }
    memmove(qp->iph, iph, ihlen + 8);
    qp->len = 0;
    qp->ihlen = ihlen;
    qp->fragments = NULL;
    qp->hf = this_host;

    /* Start a timer for this entry. */
    qp->timer.expires = jiffies() + IP_FRAG_TIME;	/* about 30 seconds     */
    qp->timer.data = (unsigned long) qp;	/* pointer to queue     */
    addTimer(&qp->timer);

    /* Add this entry to the queue. */
    qp->prev = NULL;
    qp->next = this_host->ipqueue;
    if (qp->next != NULL)
        qp->next->prev = qp;
    this_host->ipqueue = qp;

    return (qp);
}


void Ip_fragment::ipEvictor(void)
{
    // fprintf(stderr, "ip_evict:numpack=%i\n", numpack);
    while (this_host && this_host->ip_frag_mem > IPFRAG_LOW_THRESH)
    {
        if (!this_host->ipqueue)
        {
            LOG_FATAL<<"ipEvictor: memcount";
            exit(1);
        }
        ipFree(this_host->ipqueue);
    }
}


//一个队列超时,清除这个队列
void Ip_fragment::ipExpire(unsigned long arg)
{
    struct ipq *qp;

    qp = (struct ipq *) arg;

    /* Nuke the fragment queue. */
    ipFree(qp);
}



void Ip_fragment::ipFree(struct ipq *qp)
{
    struct ipfrag *fp;
    struct ipfrag *xp;

    /* Stop the timer for this entry. */
    delTimer(&qp->timer);

    /* Remove this entry from the "incomplete datagrams" queue. */
    if (qp->prev == NULL)
    {
        this_host->ipqueue = qp->next;
        if (this_host->ipqueue != NULL)
            this_host->ipqueue->prev = NULL;
        else
            rmthisHost();
    }
    else
    {
        qp->prev->next = qp->next;
        if (qp->next != NULL)
            qp->next->prev = qp->prev;
    }
    /* Release all fragment data. */
    fp = qp->fragments;
    while (fp != NULL)
    {
        xp = fp->next;
        fragKfreeskb(fp->skb, FREE_READ);
        fragKfrees(fp, sizeof(struct ipfrag));
        fp = xp;
    }
    /* Release the IP header. */
    fragKfrees(qp->iph, 64 + 8);

    /* Finally, release the queue descriptor itself. */
    fragKfrees(qp, sizeof(struct ipq));
}

void Ip_fragment::atomicSub(int ile, int *co)
{
    *co -= ile;
}

void Ip_fragment::atomicAdd(int ile, int *co)
{
    *co += ile;
}

void Ip_fragment::kfreeSkb(struct sk_buff *skb, int type)
{
    (void)type;
    free(skb);
}

void Ip_fragment::addTimer(struct timer_list *x)
{
    if (timer_tail)
    {
        timer_tail->next = x;
        x->prev = timer_tail;
        x->next = 0;
        timer_tail = x;
    }
    else
    {
        x->prev = 0;
        x->next = 0;
        timer_tail = timer_head = x;
    }
}

void Ip_fragment::delTimer(struct timer_list *x)
{
    if (x->prev)
        x->prev->next = x->next;
    else
        timer_head = x->next;
    if (x->next)
        x->next->prev = x->prev;
    else
        timer_tail = x->prev;
}


void Ip_fragment::fragKfreeskb(struct sk_buff *skb, int type)
{
    if (this_host)
        atomicSub(skb->truesize, &this_host->ip_frag_mem);
    kfreeSkb(skb, type);
}

void Ip_fragment::fragKfrees(void *ptr, int len)
{
    if (this_host)
        atomicSub(len, &this_host->ip_frag_mem);
    free(ptr);
}

void* Ip_fragment::fragKmalloc(int size, int dummy)
{
    void *vp = (void *) malloc(size);
    (void)dummy;
    if (!vp)
        return NULL;
    atomicAdd(size, &this_host->ip_frag_mem);

    return vp;
}

/* Create a new fragment entry. */
ipfrag* Ip_fragment::ip_frag_create(int offset, int end, struct sk_buff * skb, unsigned char *ptr)
{
    struct ipfrag *fp;

    fp = (struct ipfrag *) fragKmalloc(sizeof(struct ipfrag), GFP_ATOMIC);
    if (fp == NULL)
    {
        // NETDEBUG(printk("IP: frag_create: no memory left !\n"));
        LOG_ERROR << "ip_frag_create is not create";
        exit(1);
    }
    memset(fp, 0, sizeof(struct ipfrag));

    /* Fill in the structure. */
    fp->offset = offset;
    fp->end = end;
    fp->len = end - offset;
    fp->skb = skb;
    fp->ptr = ptr;

    /* Charge for the SKB as well. */
    this_host->ip_frag_mem += skb->truesize;

    return (fp);
}

//生成目的地址的hash值
int Ip_fragment::fragIndex(struct ip *iph)
{
    unsigned int ip = ntohl(iph->ip_dst.s_addr);
    return (ip % hash_size);
}

int Ip_fragment::hostfragFind(struct ip *iph)
{
    int hash_index = fragIndex(iph);
    struct hostfrags *hf;

    this_host = 0;
    for (hf = fragtable[hash_index]; hf; hf = hf->next)
        if (hf->ip == iph->ip_dst.s_addr)
        {
            this_host = hf;
            break;
        }
    if (!this_host)
        return 0;
    else
        return 1;
}

void Ip_fragment::hostfragCreate(struct ip *iph)
{
    struct hostfrags *hf = mknew(struct hostfrags);
    int hash_index = fragIndex(iph);

    hf->prev = 0;
    hf->next = fragtable[hash_index];
    if (hf->next)
        hf->next->prev = hf;
    fragtable[hash_index] = hf;
    hf->ip = iph->ip_dst.s_addr;
    hf->ipqueue = 0;
    hf->ip_frag_mem = 0;
    hf->hash_index = hash_index;
    this_host = hf;
}

void Ip_fragment::rmthisHost()
{
    int hash_index = this_host->hash_index;

    if (this_host->prev)
    {
        this_host->prev->next = this_host->next;
        if (this_host->next)
            this_host->next->prev = this_host->prev;
    }
    else
    {
        fragtable[hash_index] = this_host->next;
        if (this_host->next)
            this_host->next->prev = 0;
    }
    free(this_host);
    this_host = 0;
}

/*
  Find the correct entry in the "incomplete datagrams" queue for this
  IP datagram, and return the queue entry address if found.
*/
ipq* Ip_fragment::ipFind(struct ip *iph)
{
    struct ipq *qp;
    struct ipq *qplast;

    qplast = NULL;
    for (qp = this_host->ipqueue; qp != NULL; qplast = qp, qp = qp->next)
    {
        if (iph->ip_id == qp->iph->ip_id &&
            iph->ip_src.s_addr == qp->iph->ip_src.s_addr &&
            iph->ip_dst.s_addr == qp->iph->ip_dst.s_addr &&
            iph->ip_p == qp->iph->ip_p)
        {
            delTimer(&qp->timer);	/* So it doesn't vanish on us. The timer will
				   be reset anyway */
            return (qp);
        }
    }
    return (NULL);
}

