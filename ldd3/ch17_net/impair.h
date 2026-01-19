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
    unsigned int q_index;
    struct napi_struct napi;
    /* act as our interrupt but it just is a timer*/
    struct timer_list timer;

    struct impair_priv *priv;
};

#define IMPAIR_MC_CAM_SIZE 16
struct rx_filter_state {
    bool promisc;
    bool allmulti;
    u8 primary_mac[ETH_ALEN];
    u8 mc_cam[IMPAIR_MC_CAM_SIZE][ETH_ALEN];
    u16 mc_cam_len;
};


struct impair_priv{
    struct net_device *dev;
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