#include <linux/init.h>
#include <linux/etherdevice.h>
#include <linux/netdev.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/circ_buf.h>
#include <linux/timer.h>

static unsigned int q_vectors = 2;
static unsigned int delay_msecs = 100;
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
};
/* NAPI poll function */
static int netty_poll(struct napi_struct *napi, int budget)
{
    struct impair_q_vector *q_vec = container_of(napi, struct impair_q_vector, napi);
    struct impair_private *priv = q_vec->priv;
    int work_done = 0;

    /* RX: pull packets from RX ring up to 'budget', call netif_receive_skb() */
    /* TX: clean completed TX descriptors */

    /* ... pretend we did no work ... */

    if (work_done < budget) {
        napi_complete_done(napi, work_done);
        /* Re-enable interrupts now that we’re out of poll mode */
        /* writel(INT_MASK_ALL, priv->ioaddr + IMASK); */
    }

    return work_done;
}
static void impl_timer_handler(struct timer_list *t)
{
    struct impair_q_vector *qv = container_of(t, struct impair_q_vector, timer);

    /* "interrupt": schedule NAPI */
    napi_schedule(&qv->napi);

    /* re-arm */
    mod_timer(&qv->timer, jiffies + msecs_to_jiffies(delay_msecs));

}


static int impair_alloc_q_vectors(struct impair_priv *priv, unsigned int num_qs)
{
    int ret = 0;
    unsigned int i;
    priv->num_q_vectors = num_qs;
    priv->q_vect = kcalloc(num_qs, sizeof(*priv->q_vect), GFP_KERNEL);
    if (!priv->q_vect) return -ENOMEM;
    for (i=0; i<num_qs; i++)
    {

        struct impair_q_vector *qv = priv->q_vect + i;
        qv->q_index = i;
        qv->priv = priv;
        memset(&qv->rx_ring, 0, sizeof(qv->rx_ring));
        memset(&qv->tx_ring, 0, sizeof(qv->tx_ring));
        /* susually activate rx/tx in hw */

        int ret = ring_init(&qv->rx_ring, RING_SIZE);
        if (ret)
        {
                // here unwind free
                return ret;
        }

        ret = ring_init(&qv->tx_ring, RING_SIZE);
        if (ret)
        {
            // here unwind free
            return ret;
        }
        netif_napi_add(priv->dev, &qv->napi, netty_poll); // not sure
        timer_setup(&qv->timer, impl_timer_handler, 0);
        napi_enable(&qv->napi);
        mod_timer(&qv->timer, jiffies + msecs_to_jiffies(delay_msecs)); // kind of like request_irq


    }
    return ret;

}

static void impair_free_q_vectors(struct impair_priv *priv)
{

    for (int i=0; i<priv->num_q_vectors; i++)
    {
        struct impair_q_vector *q = priv->q_vect + i;
        timer_shutdown_sync(&q->timer); // kind of like free_irq
        napi_disable(&q->napi);
        netif_napi_del(&q->napi);
        /* todo: what if napi is using skb; holding a ref to skb*/

        ring_free_all(&q->rx_ring);
        ring_free_all(&q->tx_ring);
    }

}
static int netty_rx(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev)
{
    /* executes in napi poll */
    struct impair_priv *priv = netdev_priv(dev);
    return 0;

}
static netdev_tx_t netty_tx(struct sk_buff *skb, struct net_device *dev)
{
    struct impair_priv *priv = netdev_priv(dev);

    // (1) map this buffer to dma
    // if (dev->features & NETIF_F_GSO)
    u16 q = skb_get_queue_mapping(skb); // given this num by ndo_select_queue
    struct impair_q_vector *ring = priv->q_vect + q;
    // find out whnat tail is next


    // (2) send it to the hardware - queue of hw tx ring


    if (skb->len > 1500)
    {

    }



}


static int netty_open(struct net_device *dev)
{
    /** called when the interface is enabled
     */
    int ret;
    struct impair_priv *priv = netdev_priv(dev);

    /* susually activate rx/tx in hw */
    ret = impair_alloc_q_vectors(priv, q_vectors);
    if (ret)
    {
        return ret;
    }
    // usually  enable napi
    // last step is to enable the carrier
    netif_carrier_on(dev);

    // netif_tx_start_all_queues(dev);
   return 0;
}
static int netty_close(struct net_device *dev)
{
    struct impair_priv *priv = netdev_priv(dev);
    netif_carrier_off(dev);
    /* susually activate rx/tx in hw */
    impair_free_q_vectors(priv);
    return 0; // clears IFF_Up flag
}
static int mtu_set(struct net_device *dev, int new_mtu)
{
    // the kernel will change to the new_mtu if this function suceeds
    if (dev->mtu == new_mtu) return 0;
    // dont allow value change
    return -EINVAL;

}
static void netty_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
    // dont know if we take a lock here
    struct impair_priv *priv = netdev_priv(dev);
    spin_lock(&priv->lock);
    *stats = priv->stats64;
    spin_unlock(&priv->lock);

}
static struct net_device_ops netty_ops = {
    .ndo_open = netty_open,
    .ndo_stop = netty_close,
    .ndo_change_mtu = mtu_set,
    .ndo_start_xmit = netty_tx,
    .ndo_get_stats64 = netty_get_stats64,

};

void netty_setup(struct net_device *dev)
{
    struct impair_priv *priv = netdev_priv(dev);
    /* ether_setup */
    ether_setup(dev);
    /* Override etherdev defaults */
    dev->mtu = 1500;
    dev->hard_header_len = sizeof(struct ethhdr);
    dev->netdev_ops = &netty_ops;
    /* set before register_netdevice */
    // offloads
    dev->hw_features = NETIF_F_SG | NETIF_F_RXCSUM | NETIF_F_GSO; // todo
    dev->features = dev->hw_features;
    dev->vlan_features = dev->features;

    /* set random dev->dev_addr */
    eth_hw_addr_random(dev);
    priv->dev_addr = &dev->dev_addr;
    // dev->irq

    dev->netdev_ops = &netty_ops;
    dev->ethtool_ops = NULL;

   

}
static int __init net_impair_init(void){
    struct net_device *netty;
    int ret;
    /**
     * dev->name: netty
     *
     */
    
    netty = alloc_netdev_mqs(sizeof(struct impair_priv), "netty", NET_NAME_UNKNOWN,
        netty_setup, q_vectors, q_vectors);
    struct impair_priv *priv = netdev_priv(netty);

    if (!netty) return -ENOMEM;
    ret = register_netdevice(netty);
    if (ret)
    {
        free_netdev(netty);
        goto free_netdev;
    }

    return 0;

    free_netdev:
        free_netdev(netty);

    return ret;
}
static void __exit net_impair_exit(void){
}
module_init(net_impair_init);
module_exit(net_impair_exit);