#include <linux/init.h>
#include <linux/etherdevice.h>
#include <linux/netdev.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/timer.h>

static unsigned int q_vectors = 2;
static unsigned int delay_msecs = 100;
#define RING_SIZE 1024
struct impair_desc
{
    __le16 cmd;
    __le16 len;
    __le16 flags;
    __le32 *data;
};
struct impair_ring
{
    unsigned char *buf[RING_SIZE];
    struct impair_desc *desc[RING_SIZE];
    unsigned int size;
    unsigned int head, tail;
};

struct impair_priv;
struct impair_q_vector
{
    /* napi <-> rx/tx <-> timer */
    struct impair_ring *rx_ring, *tx_ring;
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

static int init_ring(struct impair_ring *ring)
{
    ring->size = RING_SIZE;
    for (unsigned int i = 0; i < RING_SIZE; i++)
    {

        ring->buf[i] = kmalloc(sizeof(unsigned char *), GFP_KERNEL);
        ring->desc[i] = kmalloc(sizeof(struct impair_desc), GFP_KERNEL);
        // todo: handle errors
    }
    ring->head = ring->tail = 0;
    return 0;

}
static int netty_open(struct net_device *dev)
{
    /** called when the interface is enabled
     */
    int ret;
    struct impair_priv *priv = netdev_priv(dev);

    /* susually activate rx/tx in hw */
    for (unsigned int i = 0; i < priv->num_q_vectors; i++)
    {

        struct impair_q_vector *q_vect = &priv->q_vect[i];
        ret = init_ring(q_vect->rx_ring);
        if (ret)
        {
            return ret;
        };
        ret = init_ring(q_vect->tx_ring);
        if (ret) return ret;
        napi_enable(&q_vect->napi);
        mod_timer(&q_vect[i].timer, jiffies + msecs_to_jiffies(delay_msecs)); // kind of like request_irq
    }

    // usually  enable napi
    // last step is to enable the carrier
    netif_carrier_on(dev);

    // netif_tx_start_all_queues(dev);
   return 0;
    free_rx:
        // kfree(priv->rx_ring);
    free_tx:
    return ret; // interface stays down
}
static int netty_close(struct net_device *dev)
{
    struct impair_priv *priv = netdev_priv(dev);
    netif_carrier_off(dev);
    /* susually activate rx/tx in hw */
    for (unsigned int i = 0; i < priv->num_q_vectors; i++)
    {

        struct impair_q_vector *q_vect = &priv->q_vect[i];
        timer_shutdown_sync(&q_vect->timer); // kind of like free_irq
        napi_disable(&q_vect->napi);

    }

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
    struct impair_q_vector *q = container_of(t, struct impair_q_vector, timer);
    unsigned long j = jiffies;

    q->timer.expires+= jiffies_to_msecs(100);
    add_timer(&q->timer);

}

static int init_q_vectors(struct impair_priv *priv)
{
    int ret = 0;
    /* Add NAPI (disabled until ndo_open enables it) */
    for (unsigned int i = 0; i < priv->num_q_vectors; i++)
    {
        struct impair_q_vector *q_vect = priv->q_vect + i;
        // use devm so dont need to roll back

        q_vect[i].rx_ring = devm_kzalloc(&priv->dev->dev, sizeof(struct impair_ring), GFP_KERNEL);
        q_vect[i].tx_ring = devm_kzalloc(&priv->dev->dev, sizeof(struct impair_ring), GFP_KERNEL);
        if (!q_vect[i].rx_ring  || !q_vect[i].tx_ring) return -ENOMEM;
        q_vect[i].q_index = i;

        // per q setup
        // start napi - or in the start context
        netif_napi_add(priv->dev, &q_vect[i].napi, netty_poll);
        timer_setup(&q_vect[i].timer, impl_timer_handler, 0);
        q_vect[i].priv = priv;
    }
    // start the tx path
    netif_tx_start_all_queues(priv->dev);
    return ret;
    
}
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
    priv->num_q_vectors = q_vectors;
    priv->q_vect = kzalloc(sizeof(struct impair_q_vector) * q_vectors, GFP_KERNEL);
    if (!priv->q_vect)
    {
        // todo
    };
    ret = init_q_vectors(priv);
    if (ret)
    {
        goto free_netdev;
    }
    
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