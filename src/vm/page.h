#ifndef __VIM_PAGEH_H
#define __VIM_PAGEH_H

#include "list.h"
#include "hash.h"
#include <stdint.h>
#include <debug.h>

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

struct vm_entry{
	uint8_t type; /* type for VM_BIN, VM_FILE, VM_ANON */
	void *vaddr; /* virtual page number that vm_entry manages*/
	bool writable; /* If True writable on corresponding address, If false not*/

	bool is_loaded; /* flag that tells us if is_loaded on physical memory */
	struct file* file; /* file that is mapped with virtual address */
	
	/* Will be used at Memory Mapped File*/
	struct list_elem mmap_elem; /* mmap list element */
	
	size_t offset; /* offset of file that will read*/
	size_t read_bytes; /* data size written on virtual page */
	size_t zero_bytes; /* left bytes that will be filled with 0*/
	
	/* Will be used on Swapping*/
	size_t swap_slot; /* swap slot */

	/* Will be used on ‘struct for vm_entries’*/
	struct hash_elem elem; /* hash table Element */
};

struct mmap_file {
    int mapid;
    struct file* file;		/* file pointer in mem */
    struct list_elem elem;	/* for thread's mmap_list */
    struct list vme_list;	/* its vm entry list */
};

void vm_init (struct hash *vm);
bool insert_vme (struct hash *vm, struct vm_entry *vme);
bool delete_vme (struct hash *vm, struct vm_entry *vme);
struct vm_entry *find_vme (void *vaddr);
void vm_destroy (struct hash *vm);
bool load_file (void* kaddr, struct vm_entry *vme);

#endif