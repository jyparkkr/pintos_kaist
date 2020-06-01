#include "vm/swap.h"
#include "threads/thread.h"
#include <bitmap.h>
#include "devices/block.h"
#include "threads/interrupt.h"

struct bitmap *swap_bitmap;
struct lock swap_lock;
struct block *swap_slot;

#define SWAP_SLOT_SIZE 4096
/* initialize swap area */
void swap_init ()
{
    lock_init(&swap_lock);
    swap_slot = block_get_role(BLOCK_SWAP);

    int bitmap_size;
    /* bitmap size is
     disk_sector_num * 512B(disk sector size) / 4kB(swap slot size) */ 
    bitmap_size = (int) block_size(swap_slot)\
     * BLOCK_SECTOR_SIZE / SWAP_SLOT_SIZE;
    swap_bitmap = bitmap_create(bitmap_size);
}

/* copy used_index - swap slot to kaddr */
void swap_in (size_t used_index, void* kaddr)
{
    lock_acquire(&swap_lock);
    int total_idx, idx;
    total_idx = SWAP_SLOT_SIZE / BLOCK_SECTOR_SIZE;
    for (idx=0;idx<total_idx;idx++)
        block_read(swap_slot, used_index * total_idx + idx,\
         kaddr + idx * BLOCK_SECTOR_SIZE);
    bitmap_set_multiple(swap_bitmap, used_index, 1, false);
    lock_release(&swap_lock);
}

/* write on swap partition & return swap slot num */
size_t swap_out (void* kaddr)
{
    lock_acquire(&swap_lock);

    size_t swap_idx;
    swap_idx = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);
    if(swap_idx == BITMAP_ERROR)
        return BITMAP_ERROR;
    int total_idx, idx;
    total_idx = SWAP_SLOT_SIZE / BLOCK_SECTOR_SIZE;
    for (idx=0;idx<total_idx;idx++)
        block_write(swap_slot, swap_idx * total_idx + idx,\
         kaddr + idx * BLOCK_SECTOR_SIZE);
    lock_release(&swap_lock);
    return swap_idx;
}
