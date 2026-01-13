#ifndef _IMPAIR_H_
#define _IMPAIR_H_
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/circ_buf.h>
#include <linux/timer.h>

#define RING_SIZE 1024
struct impair_desc
{
    u16 len;
    u16 flags;
    void *data; /* just a sk_buff */
};
struct impair_ring
{
    struct impair_desc desc[RING_SIZE];
    u32 size;
    u32 next_to_use;
    u32 next_to_clean;
    spinlock_t lock;
};

struct impair_priv;
struct impair_q_vector
{
    /* napi <-> rx/tx <-> timer */
    struct impair_ring rx_ring, tx_ring;
    struct sk_buff_head *rx_skb_queue;
    unsigned int q_index;
    struct napi_struct napi;
    /* act as our interrupt but it just is a timer*/
    struct timer_list timer;

    struct impair_priv *priv;

};

struct rx_filter_state {
    bool promisc;
    bool allmulti;
    uint8_t primary_mac[6];
};
static inline bool is_mcast(const uint8_t mac[6]) { return (mac[0] & 1) != 0; }
static inline bool is_bcast(const uint8_t mac[6]) {
    return mac[0]==0xff && mac[1]==0xff && mac[2]==0xff &&
           mac[3]==0xff && mac[4]==0xff && mac[5]==0xff;
}
static inline bool mac_eq(const uint8_t a[6], const uint8_t b[6]) {
    return memcmp(a,b,6)==0;
}

inline bool rx_accept(const struct rx_filter_state* f, const uint8_t dst[6]) {
    if (f->promisc) return true;
    if (is_bcast(dst)) return true;
    if (is_mcast(dst)) return f->allmulti;
    return mac_eq(dst, f->primary_mac);
}


struct impair_priv{
    struct net_device *dev;
    struct bpf_prog *prog;
    unsigned int num_q_vectors;
    struct impair_q_vector *q_vect;

    /* pointer to dev->dev_addr */
    const unsigned char **dev_addr;
    spinlock_t lock;
    struct rtnl_link_stats64 stats64;
    unsigned int delay_msecs;
    /* hw-virtual rx mac filters - state changes fill these*/
    struct rx_filter_state rx_filter;

};

/* ring */
u32 ring_next(u32 index);
u32 ring_free(const struct impair_ring *r);
u32 ring_used(const struct impair_ring *r);
int ring_init(struct impair_ring *r, u32 size);
void ring_free_all(struct impair_ring *r);

/* q_vectors */
int impair_alloc_q_vectors(struct impair_priv *priv, unsigned int num_qs);
void impair_free_q_vectors(struct impair_priv *priv);

#endif