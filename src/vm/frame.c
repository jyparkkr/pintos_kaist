#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/thread.h"

struct list lru_list;
struct lock lru_list_lock;
struct list_elem *lru_clock; // clock pointed for  swap

static struct list_elem* get_next_lru_clock(void);


/* initialize lru_list for swapping */
void lru_list_init (void)
{
    list_init (&lru_list);
    lock_init (&lru_list_lock);
    lru_clock = NULL;
}

/* add user page on end of LRU list */
void add_page_to_lru_list (struct page* page)
{
    ASSERT(page);
    lock_acquire (&lru_list_lock);
    list_push_back(&lru_list, &page->lru);
    lock_release (&lru_list_lock);
}

/* delete user page from LRU list */
void del_page_from_lru_list (struct page* page)
{
    ASSERT(page);
    ASSERT(!list_empty(&lru_list));
    if(&page->lru == lru_clock)
    {
        if(lru_clock != list_begin(&lru_list)){
            lru_clock = list_prev(lru_clock);
            ASSERT(lru_clock != list_end(&lru_list));
        }
        else
        {
            lru_clock = list_prev(list_end(&lru_list));
            ASSERT(lru_clock != list_end(&lru_list));
        }
    }
    list_remove(&page->lru);
}

/* update lru clock and return next element */
static struct list_elem* 
get_next_lru_clock(void)
{
    ASSERT(lru_clock != list_end(&lru_list));
    if (lru_clock == list_end(&lru_list)){
        lru_clock = list_begin(&lru_list);
        ASSERT(lru_clock != list_end(&lru_list));
        return lru_clock;
    }
    if (list_empty (&lru_list))
        return NULL;
    if (lru_clock == NULL){
        lru_clock = list_begin(&lru_list);
        ASSERT(lru_clock != list_end(&lru_list));
    }
    else if (list_next(lru_clock) == list_end(&lru_list)){
        lru_clock = list_begin(&lru_list);
        ASSERT(lru_clock != list_end(&lru_list));
    }
    lru_clock = list_next (lru_clock);
    ASSERT(lru_clock != list_end(&lru_list));

    return lru_clock;
}

/* find page of corresponding physical addr from lru_list */
struct page* 
find_page_from_lru_list (void* kaddr)
{
    ASSERT(kaddr);
    struct list_elem* e;
    struct page *pg;
	for (e = list_begin (&lru_list); e != list_end (&lru_list);
		e = list_next (e))
	{
		pg = list_entry (e, struct page, lru);
        if (pg->kaddr == kaddr)
            return pg;
	}
    return NULL;
}

/* when lack of free phyical memory -> get by clock alogrithm */
void try_to_free_pages (enum palloc_flags flags UNUSED)
{
    lock_acquire(&lru_list_lock);
    /* get victim page to free by clock algorithm */
    struct list_elem *e;
    struct page *pg;
    e = get_next_lru_clock(); 
    ASSERT (e != list_end(&lru_list));
    /* this page may not be free if accessed bit != 0 */
	pg = list_entry (e, struct page, lru);

    ASSERT (pg != NULL);
    ASSERT (pg->thread != NULL);
    ASSERT (pg->vme != NULL);

    /* free or change accessed bit of target from above */
    uint32_t *pd;
    void *vpage;
    pd = pg->thread->pagedir;
    vpage = pg->vme->vaddr;
    /* second_chance algorithm */
    ASSERT (pd != NULL);
    if (pagedir_is_accessed (pd, vpage))
    {
        pagedir_set_accessed (pd, vpage, false);
        lock_release(&lru_list_lock);
        return;
    }
    
    bool dirty;
    dirty = pagedir_is_dirty(pd, vpage);
    switch (pg->vme->type)
    {
        case VM_BIN:
            if(dirty)
            {
                pg->vme->swap_slot = swap_out (pg->kaddr);
                pg->vme->type = VM_ANON;
            }
            break;
        case VM_FILE:
            if(dirty)
            {
                lock_acquire(&filesys_lock);
                file_write_at(pg->vme->file, pg->vme->vaddr,\
                 pg->vme->read_bytes, pg->vme->offset);
                lock_release(&filesys_lock);
            }
            break;
        case VM_ANON:
            pg->vme->swap_slot = swap_out (pg->kaddr);
            break;
    }
    __free_page(pg);
    pg->vme->is_loaded = false;
    lock_release(&lru_list_lock);
}
