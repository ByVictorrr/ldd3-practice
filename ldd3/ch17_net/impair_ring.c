/* netty_ring.c */
#include <linux/slab.h>
#include "netty.h"

u32 ring_next(u32 index) { return (index + 1) % RING_SIZE; }

u32 ring_free(const struct impair_ring *r) {
    return CIRC_SPACE(r->next_to_use, r->next_to_clean, r->size);
}

u32 ring_used(const struct impair_ring *r) {
    return CIRC_CNT(r->next_to_use, r->next_to_clean, r->size);
}

int ring_init(struct impair_ring *r, u32 size)
{
    r->size = size;
    r->next_to_use = 0;
    r->next_to_clean = 0;
    spin_lock_init(&r->lock);

    r->desc = kcalloc(size, sizeof(*r->desc), GFP_KERNEL);
    return r->desc ? 0 : -ENOMEM;
}

void ring_free_all(struct impair_ring *r)
{
    if (!r || !r->desc) return;
    kfree(r->desc);
    r->desc = NULL;
}
