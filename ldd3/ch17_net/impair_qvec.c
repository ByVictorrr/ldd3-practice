/* impair_qvec.c */
#include <linux/jiffies.h>
#include <linux/etherdevice.h>
#include "impair.h"


static int  impair_rx_drain(struct napi_struct *napi, struct impair_q_vector *qv, int budget)
{
    // (1) check if at least one desc is avail
    struct impair_ring *ring = &qv->rx_ring;
    struct impair_priv *priv = qv->priv;
    int work_done = 0;
    u32 *idx = &ring->next_to_clean;
    struct impair_desc *desc;

    if (ring_free(ring) < 1) return 0;
    while (ring_free(ring) && work_done < budget)
    {
        /* pseudo data we got from the "NIC" */
        desc = &ring->desc[*idx];
        struct sk_buff *skb = desc->data;
        u32 len = desc->len;
        u32 status = desc->flags;
        // 1. usually check status for errors like crc, etc
        // 2. Sets up the metadata
        // 2.1: parse the eth hdr
        skb->protocol = eth_type_trans(skb, qv->priv->dev);
        // 3. set offloads; have the kernel calculate it
        skb->ip_summed = CHECKSUM_NONE;
        // 4. push the paket to the networking stack
        napi_gro_receive(&qv->napi, skb);
        /* this will pass to the protocol dispatch to correct handler via*/


        // 5. replenish - dont need to b/ tx feedback
        // 6. check the work done
        if (napi_complete_done(napi, work_done))
        {
            return work_done;
        }


        // TODO: loop back to rx
        // (1.5) add all the offloads
        // (2) create the desc and then add it to the ring
        *idx ++;
        work_done++;
        spin_lock(&priv->lock);
        priv->stats64.rx_packets++;
        priv->stats64.rx_bytes += len;
        spin_unlock(&priv->lock);

    }
    return work_done;

}
static int impair_poll(struct napi_struct *napi, int budget)
{
    struct impair_q_vector *qv = container_of(napi, struct impair_q_vector, napi);
    struct impair_priv *priv = qv->priv; /* (unused for now) */
    int work_done = 0;

    /* TODO: RX pull from qv->rx_ring up to budget, netif_receive_skb() */


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
