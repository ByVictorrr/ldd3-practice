#include <linux/init.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/circ_buf.h>
#include <linux/timer.h>
#include "impair.h"

static unsigned int q_vectors = 2;
static unsigned int delay_msecs = 100;


static netdev_tx_t impair_tx(struct sk_buff *skb, struct net_device *dev)
{
    struct impair_priv *priv = netdev_priv(dev);

    // (1) map this buffer to dma
    // if (dev->features & NETIF_F_GSO)
    u16 q = skb_get_queue_mapping(skb); // given this num by ndo_select_queue
    struct impair_q_vector *qv = priv->q_vect + q;
    struct impair_ring *tx = &qv->tx_ring;
    struct netdev_queue *txq = netdev_get_tx_queue(dev, q);
    u32 idx;

    /* TODO: check the linearlity of skb */
    spin_lock(&tx->lock);
    /* producer: enqueue into TX RING - check if we at least have one space open */
    if (ring_free(tx) < 1)
    {
        // stop the transmit queue
        netif_tx_stop_queue(txq); // tells the kernel that this has too many (dont call impair_tx)
        spin_unlock(&tx->lock);
        return NETDEV_TX_BUSY;
    }
    idx = tx->next_to_use;
    /* Fill descriptor (virutal) */
    tx->desc[idx].data = skb;
    tx->desc[idx].len = skb->len;
    tx->desc[idx].flags = 0; // todo; add offloads; HW will later set done
    /* advance tail (next_to_use) */
    tx->next_to_use = ring_next(idx);
    spin_unlock(&tx->lock);

    /** TODO: need to scheule/kick hw*/
    spin_lock(&qv->rx_ring.lock);
    idx = ring_next(qv->rx_ring.next_to_use);
    /* kind of pointless to have to two rings */
    qv->rx_ring.desc[idx].data = skb;
    spin_unlock(&qv->rx_ring.lock);

    return NETDEV_TX_OK;



    // (2) send it to the hardware - queue of hw tx ring


    if (skb->len > 1500)
    {

    }



}

static void impair_tx_timeout(struct net_device *dev, unsigned int tx_q)
{
    /* this is called when txq->qdisc present
     * and IFF_UP and carrier is up
     * and one tx is stopped
     * and txq->trans_start + dev->watchdog_timeo > jiffies
     */
    struct impair_priv *priv = netdev_priv(dev);

    priv->stats64.tx_errors++;
    priv->stats64.tx_dropped++;
    /*  index the subqueue index that timed out*/
    struct netdev_queue * q = netdev_get_tx_queue(dev, tx_q);
    /* TODO: clean/flush that queues TX ring, kick HW, eetc */
    if (netif_tx_queue_stopped(q))
        netif_wake_subqueue(dev, tx_q);
}

static int impair_open(struct net_device *dev)
{
    /** called when the interface is enabled
     */
    int ret;
    struct impair_priv *priv = netdev_priv(dev);

    /* susually activate rx/tx in hw */
    /* Step 1 - alloc tx/rx rings and start timer */
    ret = impair_alloc_q_vectors(priv, q_vectors);
    if (ret)
    {
        return ret;
    }
    /* Step 2 - set carrier as on */
    netif_carrier_on(dev); // usually conitionally set it up

    /* step 3 - tell kernel to start accepting tx packets */
    netif_tx_start_all_queues(dev);
   return 0;
}
static int impair_close(struct net_device *dev)
{
    struct impair_priv *priv = netdev_priv(dev);
    /* step 1 - stop all queues from accepting tx packets - waits for in-flight to finish */
    netif_tx_stop_all_queues(dev);
    /* step 2 - carrier is off (usually conditionally checked) */
    netif_carrier_off(dev);
    /* step 3 - free rx/tx ring resources */
    impair_free_q_vectors(priv);
    return 0; // clears IFF_Up flag
}
static int impair_mtu_set(struct net_device *dev, int new_mtu)
{
    /**
     * (1) validate new_mtu (core checks between dev->{min,max}_mtu
     * (2) reconfigure anything mtu dependent (rx buf size, limits, offloads)
     * (3) update the value*/
    // the kernel will change to the new_mtu if this function suceeds
    if (dev->mtu == new_mtu)
    {
        dev->mtu = new_mtu;
        return 0;
    }
    // dont allow value change

    return -EINVAL;

}
static void impair_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
    // dont know if we take a lock here
    struct impair_priv *priv = netdev_priv(dev);
    spin_lock(&priv->lock);
    *stats = priv->stats64;
    /* common stats
     *  RX/TX bytes: priv->stats64.rx_bytes, priv->stats64.tx_bytes
     *  errors: pkts failed to send or bad packets recv
     *  dropped: pkts the driver had to drop due to lack of resources
     */
    spin_unlock(&priv->lock);

}
static int impair_set_mac_address(struct net_device *dev, void *p)
{
    struct sockaddr *addr = p;
    struct impair_priv *priv = netdev_priv(dev);
    /* step 1 - validate that you can change mac address if the intf is up/running
     *  this is suppported if dev->priv_flags & IFF_LIVE_ADDR_CHANGE
     */
    int ret = eth_prepare_mac_addr_change(dev, p);
    if (ret)
        return ret;

    spin_lock(&priv->lock);
    ether_addr_copy(priv->rx_filter.primary_mac, addr->sa_data);
    spin_unlock(&priv->lock);


    /* now actually update dev->dev_addr*/
    eth_commit_mac_addr_change(dev, p);
    return 0;
}

static void impair_set_rx_mode(struct net_device *dev)
{
    struct impair_priv *priv = netdev_priv(dev);
    struct rx_filter_state newf;

    /* snapshot */
    newf.promisc = !!(dev->flags & IFF_PROMISC);

    /*
     * called when:
     *  1. rx-mode flag changes
     *      a: promisc toggles: ip link dev ... promisc on/off
     *      b: all multicast toggles: ip link set ... allmulticast on/off
     *  2. the unicast or multicast filters list changes
     *      a: multicast join/leaves - ip maddr add/del .. dev
     *      b: unicast list changes: extra MACs for bridges/vlan/bonding
     *  3. Upper-device sync events: when a bridge/bond/vlan/team macllan needs to push its (uc/mc) lists down toe the
     *      phy port, it does the sync and calls on this
     *
     * Since we don't keep a multicast allow-list in rx_filter_state,
     * the only safe options are: accept all multicast or accept none.
     *
     * Linux will add needed multicast addresses (IPv6, etc.) to dev's mc list.
     * If you ignore that list, you'll break IPv6 and other protocols.
     *
     * So: accept all multicast if:
     *  - IFF_ALLMULTI is set, OR
     *  - there exists any multicast address in the list.
     */
    newf.allmulti = !!(dev->flags & IFF_ALLMULTI) || !netdev_mc_empty(dev);

    /* primary MAC */
    ether_addr_copy(newf.primary_mac, dev->dev_addr);

    /* commit */
    spin_lock(&priv->lock);
    priv->rx_filter = newf;
    spin_unlock(&priv->lock);
    /* dev->mc */

}

static int impair_bpf(struct net_device* dev, struct netdev_bpf* bpf)
{

}
static struct net_device_ops impair_ops = {
    .ndo_open = impair_open,
    .ndo_stop = impair_close,
    /* change certain net_device fields */
    .ndo_change_mtu = impair_mtu_set, // dev->mtu size change
    .ndo_set_rx_mode = impair_set_rx_mode, // dev->flags state change for rx filter
    .ndo_set_mac_address = impair_set_mac_address, // dev->dev_addr change
    /* get the stats */
    .ndo_get_stats64 = impair_get_stats64,

    .ndo_bpf = impair_bpf,
    /* timeout from watchdog of a tx queue */
    .ndo_tx_timeout = impair_tx_timeout,
    .ndo_start_xmit = impair_tx,


};

void impair_setup(struct net_device *dev)
{
    struct impair_priv *priv = netdev_priv(dev);
    /* ether_setup */
    ether_setup(dev);
    /* Override etherdev defaults */
    dev->mtu = 1500;
    dev->hard_header_len = sizeof(struct ethhdr);
    dev->netdev_ops = &impair_ops;
    /* set before register_netdevice */
    // offloads
    dev->hw_features = NETIF_F_SG | NETIF_F_RXCSUM | NETIF_F_GSO; // todo
    dev->features = dev->hw_features;
    dev->vlan_features = dev->features;

    /* set random dev->dev_addr */
    eth_hw_addr_random(dev);
    priv->dev_addr = &dev->dev_addr;
    // dev->irq
    priv->delay_msecs = delay_msecs;
    priv->num_q_vectors = q_vectors;
    memset(&priv->rx_filter, 0, sizeof(priv->rx_filter)); // state filter

    dev->netdev_ops = &impair_ops;
    dev->ethtool_ops = NULL;

   

}
static int __init net_impair_init(void){
    struct net_device *impair;
    int ret;
    /**
     * dev->name: impair
     *
     */
    
    impair = alloc_netdev_mqs(sizeof(struct impair_priv), "netty", NET_NAME_UNKNOWN,
        impair_setup, q_vectors, q_vectors);
    struct impair_priv *priv = netdev_priv(impair);

    if (!impair) return -ENOMEM;
    ret = register_netdevice(impair);
    if (ret)
    {
        free_netdev(impair);
        goto free_netdev;
    }

    return 0;

    free_netdev:
        free_netdev(impair);

    return ret;
}
static void __exit net_impair_exit(void){
}
module_init(net_impair_init);
module_exit(net_impair_exit);