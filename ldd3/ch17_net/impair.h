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
    struct sk_buff *dkb; /* virtual: the "buffer" */
};
struct impair_ring
{
    struct impair_desc *desc;
    u32 size;
    u32 next_to_use;
    u32 next_to_clean;
    spinlock_t lock;
};
static inline u32 ring_next(u32 index) { return (index + 1) % RING_SIZE; }
static inline u32 ring_free(const struct impair_ring *r) { return CIRC_SPACE(r->next_to_use, r->next_to_clean, r->size); }
static inline u32 ring_used(const struct impair_ring *r){ return CIRC_CNT(r->next_to_use, r->next_to_clean, r->size); }
static int ring_init(struct impair_ring *r, u32 size)
{
    r->size = size;
    r->next_to_use = 0;
    r->next_to_clean = 0;
    spin_lock_init(&r->lock);

    r->desc = kcalloc(size, sizeof(*r->desc), GFP_KERNEL);
    return r->desc ? 0 : -ENOMEM;
}

static void ring_free_all(struct impair_ring *r)
{
    if (!r || !r->desc) return;
    kfree(r->desc);
    r->desc = NULL;
}
struct impair_priv;
struct impair_q_vector
{
    /* napi <-> rx/tx <-> timer */
    struct impair_ring rx_ring, tx_ring;
    unsigned int q_index;
    struct napi_struct napi;
    /* act as our interrupt but it just is a timer*/
    struct timer_list timer;

    struct impair_priv *priv;

};

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
};


#endif