#ifndef __VIM_PAGEH_H
#define __VIM_PAGE_H

#include "list.h"
#include "hash.h"
#include <stdint.h>
#include <debug.h>

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

/* memory-mapping file structure */
struct mmap_file {
    int mapid;
    struct file* file;
    struct list_elem elem;
    struct list vme_list;
};

struct vm_entry{
    ;
};

#endif