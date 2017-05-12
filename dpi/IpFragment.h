//
// Created by jyc on 17-4-14.
//
#ifndef NOFF_IP_FRAGMENT_H
#define NOFF_IP_FRAGMENT_H

#include <functional>
#include <muduo/base/noncopyable.h>
#include <vector>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>

#include "Util.h"

#define IP_CE		0x8000	/* Flag: "Congestion" */
#define IP_DF		0x4000	/* Flag: "Don't Fragment" */
#define IP_MF		0x2000	/* Flag: "More Fragments" */
#define IP_OFFSET	0x1FFF	/* "Fragment Offset" part */

#define IP_FRAG_TIME	(30 * 1000)	/* fragment lifetime */

#define UNUSED 314159
#define FREE_READ UNUSED
#define FREE_WRITE UNUSED
#define GFP_ATOMIC UNUSED
#define NETDEBUG(x)

#define IPF_NOTF 1
#define IPF_NEW  2
#define IPF_ISF  3

#define mknew(x)	(x *)malloc(sizeof(x))

#define IPFRAG_HIGH_THRESH		(256*1024)
#define IPFRAG_LOW_THRESH		(192*1024)


struct skBuff
{
    char *data;
    int truesize;
};

struct timerList
{
    struct timerList *prev;
    struct timerList *next;
    int expires;
    unsigned long data;
    // struct ipq *frags;
};

struct hostFrags
{
    struct ipq *ipqueue;
    int ip_frag_mem;
    int ip;
    int hash_index;
    struct hostFrags *prev;
    struct hostFrags *next;
};

/* Describe an IP fragment. */
struct ipFrag
{
    int offset;			/* offset of fragment in IP datagram    */
    int end;			/* last byte of data in datagram        */
    int len;			/* length of this fragment              */
    struct skBuff *skb;		/* complete received fragment           */
    unsigned char *ptr;		/* pointer into real fragment data      */
    struct ipFrag *next;		/* linked list pointers                 */
    struct ipFrag *prev;
};

/* Describe an entry in the "incomplete datagrams" queue. */
struct ipq
{
    unsigned char *mac;		/* pointer to MAC header                */
    struct ip *iph;		/* pointer to IP header                 */
    int len;			/* total length of original datagram    */
    short ihlen;			/* length of the IP header              */
    short maclen;			/* length of the MAC header             */
    struct timerList timer;	/* when will this queue expire?         */
    struct ipFrag *fragments;	/* linked list of received fragments    */
    struct hostFrags *hf;
    struct ipq *next;		/* linked list pointers                 */
    struct ipq *prev;
    // struct device *dev;	/* Device - for icmp replies */
};

class IpFragment : muduo::noncopyable
{
public:
    typedef std::function<void(ip*,int,timeval)>    IpCallback;
    typedef std::function<void(ip*,int,timeval)>    UdpCallback;
    typedef std::function<void(ip*, int,timeval)>   TcpCallback;
    typedef std::function<void(ip*,int,timeval)>    IcmpCallback;
    IpFragment();
    IpFragment(size_t n);
    ~IpFragment();

    void addIpCallback(const IpCallback& cb)
    {
        ipCallbacks_.push_back(cb);
    }

    void addTcpCallback(const TcpCallback& cb)
    {
        tcpCallbacks_.push_back(cb);
    }

    void addUdpCallback(const UdpCallback& cb)
    {
        udpCallbacks_.push_back(cb);
    }

    void addIcmpCallback(const IcmpCallback& cb)
    {
        icmpCallbacks_.push_back(cb);
    }

    void startIpfragProc(ip *data, int len,timeval timeStamp);



private:
    std::vector<IpCallback>     ipCallbacks_;
    std::vector<TcpCallback>    tcpCallbacks_;
    std::vector<UdpCallback>    udpCallbacks_;
    std::vector<IcmpCallback>   icmpCallbacks_;

    struct hostFrags **fragtable;
    struct hostFrags *this_host;
    int numpack = 0;
    int hash_size;
    int timenow;
    unsigned int time0;
    struct timerList *timer_head = 0, *timer_tail = 0;

    int ipDefragStub(struct ip *iph, struct ip **defrag);
    int jiffies();
    char* ipDefrag(struct ip *iph, struct skBuff *skb);
    char* ipGlue(struct ipq *qp);
    int ipDone(struct ipq *qp);
    ipq* ipCreate(struct ip *iph);
    void ipEvictor(void);
    void ipExpire(unsigned long arg);
    void ipFree(struct ipq *qp);
    void atomicSub(int ile, int *co);
    void atomicAdd(int ile, int *co);
    void kfreeSkb(struct skBuff *skb, int type);
    void addTimer(struct timerList *x);
    void delTimer(struct timerList *x);
    void fragKfreeskb(struct skBuff *skb, int type);
    void*fragKmalloc(int size, int dummy);
    void fragKfrees(void *ptr, int len);
    ipFrag* ip_frag_create(int offset, int end, struct skBuff * skb, unsigned char *ptr);
    int fragIndex(struct ip *iph);
    int hostfragFind(struct ip *iph);
    void hostfragCreate(struct ip *iph);
    void rmthisHost();
    void genIpProc(u_char *data, int skblen,timeval timeStamp);
    ipq* ipFind(struct ip *iph);
};


#endif //NOFF_IP_FRAGMENT_H
