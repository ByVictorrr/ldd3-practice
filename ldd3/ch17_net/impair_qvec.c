/* impair_qvec.c */
#include <linux/jiffies.h>
#include <linux/etherdevice.h>
#include "impair.h"

static bool rx_accept(const struct rx_filter_state* f, const uint8_t dst[ETH_ALEN]) {
    if (f->promisc) return true;
    if (is_broadcast_ether_addr(dst)) return true;
    if (is_multicast_ether_addr(dst))
    {
        if (f->allmulti) return true;
        for (int i = 0; i < f->mc_cam_len; i++)
        {
            if (ether_addr_equal(dst, f->mc_cam[i])) return true;
        }
        return false;

    }
    return ether_addr_equal(dst, f->primary_mac);
}


static int  impair_rx_drain(struct impair_q_vector *qv, int budget)
{
    // (1) check if at least one desc is avail
    struct impair_ring *tx = &qv->tx_ring;
    struct impair_ring *rx = &qv->rx_ring;
    struct impair_priv *priv = qv->priv;
    struct netdev_queue *txq = netdev_get_tx_queue(priv->dev, qv->q_index);

    int work_done = 0;
    while (work_done < budget)
    {
        struct sk_buff *tx_skb, *rx_skb;
        u32 idx;
        /* 1. pop from tx ring */
        spin_lock_bh(&tx->lock);
        /* is the tx ring empty? */
        if (tx->next_to_clean == tx->next_to_use)
        {
            spin_unlock_bh(&tx->lock);
            break;
        }
        idx = tx->next_to_clean;
        tx_skb = tx->desc[idx].data;
        tx->desc[idx].data = NULL;
        tx->next_to_clean = ring_next(idx);
        /* after done with tx slot; check if tx is stopped and trigger wake queue */
        if (netif_tx_queue_stopped(txq) && ring_free(tx) > 0)
            netif_wake_subqueue(priv->dev, qv->q_index);

        spin_unlock_bh(&tx->lock);
        if (!tx_skb) break;
        /* 2. create rx skb */
        rx_skb = skb_copy(tx_skb, GFP_ATOMIC);
        if (!rx_skb)
        {
            spin_lock_bh(&priv->lock);
            priv->stats64.rx_dropped++;
            spin_unlock_bh(&priv->lock);
            goto tx_complete;
        }
        /* check if the packet has at least ETH_HEADER len packets in data */
        if (!pskb_may_pull(rx_skb, ETH_HLEN))
        {
            spin_lock_bh(&priv->lock);
            priv->stats64.rx_dropped++;
            spin_unlock_bh(&priv->lock);
            kfree_skb(rx_skb);
            goto tx_complete;
        }
        const struct ethhdr *eth = (const struct ethhdr *)rx_skb->data;
        struct rx_filter_state f;
        spin_lock_bh(&priv->lock);
        f = priv->rx_filter;
        spin_unlock_bh(&priv->lock);
        /* Point MAC header at the current data, then read dst MAC */
        if (!rx_accept(&f, eth->h_dest)) {
            spin_lock_bh(&priv->lock);
            priv->stats64.rx_dropped++;
            spin_unlock_bh(&priv->lock);
            kfree_skb(rx_skb);
            goto tx_complete;
        }

        /* 3. push to rx ring */
        spin_lock_bh(&rx->lock);
        if (ring_free(rx) < 1)
        {
            spin_unlock_bh(&rx->lock);
            spin_lock_bh(&priv->lock);
            priv->stats64.rx_dropped++;
            spin_unlock_bh(&priv->lock);
            kfree_skb(rx_skb);
            goto tx_complete;
        }
        idx = rx->next_to_use;
        rx->desc[idx].data = rx_skb;
        rx->desc[idx].len = rx_skb->len;
        rx->desc[idx].flags = 0;
        rx->next_to_use = ring_next(idx);
        if (is_multicast_ether_addr(eth->h_dest))
        {
            spin_lock_bh(&priv->lock);
            priv->stats64.multicast++;
            spin_unlock_bh(&priv->lock);
        }
        spin_unlock_bh(&rx->lock);


        tx_complete:
            /* 4. tx complete */
            spin_lock_bh(&priv->lock);
            priv->stats64.tx_packets++;
            priv->stats64.tx_bytes += tx_skb->len;
            kfree_skb(tx_skb);
            spin_unlock_bh(&priv->lock);
        work_done++;
    }
    return work_done;
}
static int impair_rx_deliver(struct impair_q_vector *qv, int budget)
{

    struct impair_priv *priv = qv->priv;
    struct impair_ring *rx = &qv->rx_ring;
    int work_done = 0;
    while (work_done < budget)
    {
        struct sk_buff *skb;
        u32 idx;
        /* 1. pop from the rx ring */

        spin_lock_bh(&rx->lock);
        /* is empty ? */
        if (rx->next_to_clean == rx->next_to_use)
        {
            spin_unlock_bh(&rx->lock);
            break;
        }
        idx = rx->next_to_clean;
        skb = rx->desc[idx].data;
        rx->desc[idx].data = NULL;
        rx->next_to_clean = ring_next(idx);
        spin_unlock_bh(&rx->lock);
        if (!skb) break;

        skb->protocol = eth_type_trans(skb, priv->dev);

        /* Honor ethtool -K rx on/off (NETIF_F_RXCSUM) */
        if (priv->dev->features & NETIF_F_RXCSUM) {
            /* Simulate NIC verified L4 checksum */
            skb->ip_summed  = CHECKSUM_UNNECESSARY;
        } else {
            /* Do not claim checksum verified; let stack validate if needed */
            skb->ip_summed  = CHECKSUM_NONE;
        }

        spin_lock_bh(&priv->lock);
        priv->stats64.rx_packets++;
        priv->stats64.rx_bytes += skb->len;
        spin_unlock_bh(&priv->lock);

        napi_gro_receive(&qv->napi, skb);

        work_done++;

    }
    return work_done;
}

static int impair_poll(struct napi_struct *napi, int budget)
{
    struct impair_q_vector *qv = container_of(napi, struct impair_q_vector, napi);
    int work_done = 0;

    /* drain tx ring -> rx ring */
    work_done += impair_rx_drain(qv, budget);
    if (work_done < budget)
        work_done += impair_rx_deliver(qv, budget - work_done);

    if (work_done < budget)
        napi_complete_done(napi, work_done);

    return work_done;
}

/**
 * This function will act like the transmit interrupt per ring.
 */
static void impl_timer_handler(struct timer_list *t)
{
    struct impair_q_vector *qv = container_of(t, struct impair_q_vector, timer);
    if (napi_schedule_prep(&qv->napi))
    {
        __napi_schedule(&qv->napi);
    }
}

int impair_alloc_q_vectors(struct impair_priv *priv, unsigned int num_qs)
{
    unsigned int i;
    int ret;

    priv->num_q_vectors = num_qs;
    priv->q_vect = kcalloc(num_qs, sizeof(*priv->q_vect), GFP_KERNEL);
    if (!priv->q_vect) return -ENOMEM;

    for (i = 0; i < num_qs; i++) {
        struct impair_q_vector *qv = &priv->q_vect[i];

        qv->q_index = i;
        qv->priv = priv;

        /* Step 1 - enable the RX & TX ring */
        ret = ring_init(&qv->rx_ring, RING_SIZE);
        if (ret) return ret;

        ret = ring_init(&qv->tx_ring, RING_SIZE);
        if (ret) return ret;

        /* default weight is fine; use netif_napi_add_weight() if you want custom */
        netif_napi_add(priv->dev, &qv->napi, impair_poll);

        /* Step 2 - setup the timer (interrupts) */
        timer_setup(&qv->timer, impl_timer_handler, 0);

        /* step 3 - enable/start napi polling*/
        napi_enable(&qv->napi);
    }

    return 0;
}

void impair_free_q_vectors(struct impair_priv *priv)
{
    unsigned int i;

    if (!priv || !priv->q_vect) return;

    for (i = 0; i < priv->num_q_vectors; i++) {
        struct impair_q_vector *qv = &priv->q_vect[i];

        // stop the timer from scheduling anything
        timer_shutdown_sync(&qv->timer);
        // handles the rx side, it waits for any in-flight poll to exit
        napi_disable(&qv->napi);

        netif_napi_del(&qv->napi);

        ring_free_all(&qv->rx_ring);
        ring_free_all(&qv->tx_ring);
    }

    kfree(priv->q_vect);
    priv->q_vect = NULL;
    priv->num_q_vectors = 0;
}
