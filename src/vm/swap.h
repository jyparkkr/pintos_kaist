#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <list.h>
#include "vm/page.h"
#include "vm/frame.h"

void swap_init (void);
void swap_in (size_t used_index, void* kaddr);
size_t swap_out (void* kaddr);

#endif