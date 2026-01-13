/* impair_qvec.c */
#include <linux/jiffies.h>
#include <linux/etherdevice.h>
#include "impair.h"


static int  impair_rx_drain(struct napi_struct *napi, struct impair_q_vector *qv, int budget)
{
    // (1) check if at least one desc is avail
    struct impair_ring *ring = &qv->rx_ring;
    u32 *idx = &ring->next_to_clean;
    struct impair_desc *desc;

    if (ring_free(ring) < 1) return 0;
    while (ring_free(ring) && budget--)
    {
        desc = &ring->desc[*idx];
        u64 *buffer  = desc->data;
        u32 len = desc->len;
        u32 status = desc->flags;
        struct sk_buff *skb = netdev_alloc_skb(qv->priv->dev, len);
        qv->rx_skb_queue = skb->next;


        // TODO: loop back to rx
        // (1.5) add all the offloads
        // (2) create the desc and then add it to the ring
        *idx ++;
    }



}
static int impair_poll(struct napi_struct *napi, int budget)
{
    struct impair_q_vector *qv = container_of(napi, struct impair_q_vector, napi);
    struct impair_priv *priv = qv->priv; /* (unused for now) */
    int work_done = 0;

    /* TODO: RX pull from qv->rx_ring up to budget, netif_receive_skb() */
    /* TODO: TX clean qv->tx_ring */


    if (work_done < budget)
        napi_complete_done(napi, work_done);

    return work_done;
}

static void impl_timer_handler(struct timer_list *t)
{
    struct impair_q_vector *qv = container_of(t, struct impair_q_vector, timer);

    napi_schedule(&qv->napi);
    mod_timer(&qv->timer, jiffies + msecs_to_jiffies(qv->priv->delay_msecs));
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
        mod_timer(&qv->timer, jiffies + msecs_to_jiffies(priv->delay_msecs));
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
