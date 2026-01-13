/* netty_ring.c */
#include <linux/slab.h>
#include "impair.h"

u32 ring_next(u32 index) { return (index + 1) % RING_SIZE; }

u32 ring_free(const struct impair_ring *r) {
    return CIRC_SPACE(r->next_to_use, r->next_to_clean, r->size);
}

u32 ring_used(const struct impair_ring *r) {
    return CIRC_CNT(r->next_to_use, r->next_to_clean, r->size);
}

int ring_init(struct impair_ring *r, u32 size)
{
    if (!r || size == 0 || size > RING_SIZE)
        return -EINVAL;
    r->size = size;
    r->next_to_use = 0;
    r->next_to_clean = 0;
    spin_lock_init(&r->lock);

    memset(r->desc, 0, size * sizeof(*r->desc));
    return 0;
}

void ring_free_all(struct impair_ring *r)
{
    if (!r) return;
    kfree(r->desc);
    // r->desc = NULL;
    for (u32 i=r->next_to_clean; i!=r->next_to_use; i=ring_next(i))
    {
        if (r->desc[i].data)
        {
            // optionally could use stats here to define dropped packets ++
            dev_kfree_skb_any(r->desc[i].data);
            r->desc[i].data = NULL;
        }
    }
    r->next_to_clean = r->next_to_use = 0;
    r->size = 0;
}
