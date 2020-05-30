#include "page.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include <string.h>


static unsigned vm_hash_func (const struct hash_elem *e, void *aux UNUSED);
static bool vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
static void vm_destroy_func (struct hash_elem *, void * UNUSED);

void vm_init (struct hash *vm)
{
	/*To make sure vm is not null*/
	ASSERT (vm != NULL);
	/* initialize hash_init() using hash table*/
	hash_init (vm, vm_hash_func,vm_less_func, NULL);
}

static unsigned vm_hash_func (const struct hash_elem *e,void *aux UNUSED)
{
	ASSERT (e != NULL);
	/*search vm_entry of element using hash_entry()*/
	struct vm_entry *vme = hash_entry(e, struct vm_entry, elem);
	/* find and return hash value of vm_entry member vaddr using hash_int()*/
	return hash_int((int)vme->vaddr);
}

static bool vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
	ASSERT (a != NULL);
	ASSERT (b != NULL);
	/* search each vm_entry of each element using hash_entry()*/
	struct vm_entry *vme_a = hash_entry(a, struct vm_entry, elem);
	struct vm_entry *vme_b = hash_entry(b, struct vm_entry, elem);

	/*compare vaddr between them. true if b is bigger, false if a is bigger*/
	if(vme_a->vaddr < vme_b->vaddr)
		return true;
	return false;
}

static void vm_destroy_func(struct hash_elem *e, void *aux UNUSED)
{	/*hash bucket의 entry를 삭제 해주는 함수.*/
	ASSERT(e!=NULL);
	/* vm_entry의 메모리 제거 */
	struct vm_entry *vme = hash_entry (e, struct vm_entry, elem);
	//palloc_free_page(vme->vaddr);
  	free (vme);
}

bool insert_vme (struct hash *vm, struct vm_entry *vme)
{
	ASSERT(vm!=NULL);
	ASSERT(vme!=NULL);

	/*hash_insert return NULL when it success*/
	if(hash_insert(vm,&(vme->elem))==NULL)
		return true;   //if success, return true
	return false;
}

bool delete_vme (struct hash *vm, struct vm_entry *vme)
{
	ASSERT(vm!=NULL);
	ASSERT(vme!=NULL);

	/*delete vme from vm*/
	if(hash_delete(vm,&(vme->elem))==NULL) //if fail, return false
		return false;
	free(vme);
	return true;
}

struct vm_entry *find_vme (void *vaddr)
{
	struct vm_entry vme;
	struct hash_elem *e;

	/* get page number of vaddr by pg_round_down()*/
	vme.vaddr=pg_round_down(vaddr);
	/* get hash_elem structure using hash_find() */
	e=hash_find(&thread_current()->vm,&vme.elem);
	if(e==NULL)
		/* return null if it doesn't exist*/
		return NULL;
	else
		/* Return vm_entry of corresponding hash_elem by hash_entry()*/
		return hash_entry(e, struct vm_entry, elem);
}

void vm_destroy (struct hash *vm)
{
	ASSERT(vm!=NULL);
	/*remove vm_entries and hashtable bucketlist using hash_destroy() */
	hash_destroy (vm, vm_destroy_func);
}

bool load_file (void* kaddr, struct vm_entry *vme){
	if((int)vme->read_bytes == file_read_at(vme->file, kaddr, vme->read_bytes, vme->offset))
	{
		memset(kaddr + vme->read_bytes, 0, vme->zero_bytes);
		return true;
	} 
	return false;
}