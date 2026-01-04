#include <linux/init.h>
#include <linux/etherdevice.h>
#include <linux/netdev.h>
#include <linux/netdevice.h>
#include <linux/module.h>

static unsigned int num_tx = 10, num_rx = 10;
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
struct impair_priv{
    struct net_device *dev;
    struct bpf_prog *prog;
    struct impair_ring *rx_ring, *tx_ring;
    /* act as our interrupt but it just is a timer*/
    struct timer_list timer;
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
    priv->rx_ring = kzalloc(sizeof(struct impair_ring), GFP_KERNEL);
    if (!priv->rx_ring)
    {
        ret = -ENOMEM;
        return ret;

    }
    priv->tx_ring = kzalloc(sizeof(struct impair_ring), GFP_KERNEL);
    if (!priv->tx_ring)
    {
        ret = -ENOMEM;
        goto free_rx;
    }
    /* susually activate rx/tx in hw */
    // usually  enable napi



   return 0;
    free_rx:
        kfree(priv->rx_ring);
    free_tx:
    return ret;
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
    stats = &priv->stats64;
    spin_unlock(&priv->lock);

}
static struct net_device_ops netty_ops = {
    .ndo_open = netty_open,
    .ndo_change_mtu = mtu_set,
    .ndo_start_xmit = netty_tx,
    .ndo_get_stats64 = netty_get_stats64,

};
static void impl_timer_handler(struct timer_list *t)
{
    struct impair_priv *priv = container_of(t, struct impair_priv, timer);
    unsigned long j = jiffies;

    priv->timer.expires+= jiffies_to_msecs(100);
    add_timer(&priv->timer);

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
    timer_setup(&priv->timer, impl_timer_handler, 0);

    /* Add NAPI (disabled until ndo_open enables it) */
    netif_napi_add(dev, &priv->napi, mynic_poll);




}
static int __init net_impair_init(void){
    struct net_device *netty;
    int ret;
    /**
     * dev->name: netty
     *
     */
    netty = alloc_netdev_mqs(sizeof(struct impair_priv), "netty", NET_NAME_UNKNOWN, netty_setup, num_tx, num_rx);
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