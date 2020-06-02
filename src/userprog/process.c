#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "threads/malloc.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static bool install_page (void *upage, void *kpage, bool writable);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  //printf("process_execute\n");
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  char *save_ptr;
  int name_len;
  name_len = strlen(fn_copy) + 1;
  //printf("name_len:%d\n", name_len);
  char name[name_len];
  strlcpy (name, file_name, PGSIZE);
  strtok_r(name, " ", &save_ptr);
  //printf("name:%s\n", name);
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 
 
  return tid;
}

/* pj2 - Save parsed token to user stack. */
void
argument_stack (char **parse, int count, void **esp)
{
  //printf("~~~~~~~~argument_stack init\n");
  int align_length = 0;
  int i, j;
  /* push program name & param */
  for (i = count - 1; i > -1; i--)
  {
    for (j = strlen(parse[i]); j > -1; j--)
    {
      *esp = *esp - 1;
      **(char **)esp = parse[i][j];
      align_length++;
    }
  }
  /* Word-align */
  //printf("word_align:%d\n", (4 - (align_length % 4)) % 4);
  //printf("align_length:%d\n", align_length);

  for (i = 0; i < (4 - (-align_length % 4)) % 4;i++)
  {
    *esp = *esp - 1;
    **(uint8_t **)esp = (uint8_t)0;
    align_length += 1;
  }

  //char *argv_addr;
  for (i = count; i > -1; i--)
  {
    *esp = *esp - 4;
    if(i == count)
    {
      **(char* **)esp = (char *)0;
    }
    else
    {
      align_length -= strlen(parse[i]);
      align_length -= 1;
      **(char* **)esp = (char *) (*esp + 4*(count - i + 1) + align_length);
    }
  }
  /* argv push */
  *esp = *esp - 4;
  **(char* **)esp = (char *) (*esp + 4);
  /* argc push */
  *esp = *esp - 4;
  **(int **)esp = (int) count;
  /* fake addr */
  *esp = *esp - 4;
  **(int **)esp = (int) 0;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  //printf("~~~~~~~~start_process init\n");
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  /* pj2 - tokenize:to parse & get token num: to count */
  char *parse[127];
  int count;
  char *get_count, *save_ptr;

  count=0;
  for (get_count = strtok_r (file_name, " ", &save_ptr); get_count != NULL;
    get_count = strtok_r (NULL, " ", &save_ptr))
  {
    parse[count] = get_count;
    count++;
  }

  /*initialize hash table using vm_init()*/
  vm_init(&thread_current()->vm);

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (parse[0] ,&if_.eip, &if_.esp);
  /* after memory load, resume parent process */
  sema_up(&(thread_current()->sema_load));

  /* Set up Stack */
  if(success){
    thread_current()->load = true;
    argument_stack(parse, count, &if_.esp);
  }

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success){
    thread_current()->load = false; 
    thread_exit ();
  }
  
  /* Debugging tool - hex_dump() */
 // hex_dump(if_.esp, if_.esp, PHYS_BASE - if_.esp, true);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  /* Start the user process */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* pj2 - search child list by pid and return thread */
struct thread
*get_child_process (int pid)
{
  struct list *child_list;
  child_list = &(thread_current()->child_list);

  struct list_elem *e;
  struct thread *t;
  for (e = list_begin (child_list); e != list_end (child_list);
       e = list_next (e))
  {
    t = list_entry (e, struct thread, childelem);
    if(t -> tid == pid)
      return t;
  }
  return NULL;
}


/* search child list by pid and remove it  */
void
remove_child_process (struct thread *cp)
{
  if (cp != NULL) 
  {
    list_remove(&(cp->childelem));
    palloc_free_page(cp);
  }
}

bool handle_mm_fault (struct vm_entry *vme) 
/*allocate physical memory using palloc_get_page()*/ 
{ 
  if (vme->is_loaded) //existing page
    return false;
  struct page *kpage;
  kpage = alloc_page(PAL_USER);
  if(!kpage)
    return false;
  kpage->vme = vme;
  bool success = false;
  
/* Deal with various type of vm_entry using switch*/ 
  switch(vme->type){
    /* if VM_BIN, load on physical memory using load_file()*/
    case VM_BIN:
      success = load_file(kpage->kaddr, vme);
    break;

    case VM_FILE:
      success = load_file(kpage->kaddr, vme);
      //free_page_kaddr (kpage);
    break;

    case VM_ANON:
      swap_in (vme->swap_slot, kpage->kaddr);
      success = true;
    break;

    default:
    break;
  }
  /*map physical page and virtual page using install_page */
  if(success){
    success = install_page(vme->vaddr,kpage->kaddr, vme->writable);
    if (!success){
      free_page(kpage->kaddr);
      vme->is_loaded = true;
    }
  }
  else
    free_page(kpage->kaddr);
  
  /* load success*/ 
  return success;
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  int status;
  struct thread *child;
  /*search process descriptor of child process*/ 
  child = get_child_process(child_tid);
  /*return -1 for any exception*/
  if(child==NULL)
    return -1;

/* make parent wait until child exit */
  sema_down(&(child->sema_exit));
  status = child->exit_status;
  /*remove child process*/
  remove_child_process(child); 

  return status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* pj2.5 - destroy the current process's page directory
    and swtich back to kernel-only page directory */
  //lock_acquire(&filesys_lock);

  file_close(cur->cur_file);
  /* pj2.4 - close all opened files in current thread */
  while(cur->fd_max > 2)
  {
    cur->fd_max -= 1;
    /* file_allow_write is already implemented on process_close_file*/
    process_close_file (cur->fd_max);
  }
  //lock_release(&filesys_lock);
  /* free fd_table */
  palloc_free_page(cur->fd_table);

  /* clean vm entry and mmap_file */
  mapid_t mapid;
  for (mapid = 0;mapid < cur->next_mapid;mapid++)
    munmap(mapid);
  
  vm_destroy(&cur->vm);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}


/* add file on file description table */
int 
process_add_file (struct file *f)
{
  struct thread *t;
  int fd_max;
  t = thread_current();
  fd_max = t -> fd_max;
  /* add file obj to fd table */
  (t -> fd_table)[fd_max] = f;
  /* increase fd_max */
  t -> fd_max += 1;
  /* return fd */
  return fd_max;
}

/* pj2.4 - get file descriptor from fd_table */
struct file
*process_get_file (int fd)
{
  if(fd < thread_current()->fd_max && fd>=0)
  {
    return thread_current()->fd_table[fd];
  }
  return NULL;
}

/* pj2.4 - close file descriptor */
void 
process_close_file (int fd)
{
  struct file* f;
  f = process_get_file(fd);
  if (f != NULL)
  {
    /* close file */
    file_close(f);
    /* clear fd_table */
    thread_current()->fd_table[fd] = NULL;
  }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* acquire lock */
  lock_acquire(&filesys_lock);

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      /* release lock */
      lock_release(&filesys_lock);
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }
  /* pj2.5 - init thread's current running file */
  t->cur_file = file;
  file_deny_write(file);
  /* release lock */
  lock_release(&filesys_lock);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }
  /* release lock */
  //lock_release(&filesys_lock);

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  //file_close (file);
  return success;
}

/* load() helpers. */


/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
  {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    /* create vm_entry using malloc */
    struct vm_entry *vme = (struct vm_entry *)malloc(sizeof (struct vm_entry));
    if(vme == NULL)
      return false;

    /* Set vm_entry members, offset and size to read when virtual page required
    zero bytes for padding at last, etc*/

    memset (vme, 0, sizeof (struct vm_entry));
    vme->type = VM_BIN;
    vme->vaddr = upage;
    vme->writable    = writable;

    vme->is_loaded = false;
    vme->file = file_reopen(file);

    vme->offset = ofs;
    vme->read_bytes = page_read_bytes;
    vme->zero_bytes = page_zero_bytes;

    /* insert vm_entry created into hashtable using insert_vme() function*/
    if(insert_vme(&thread_current()->vm, vme) == false ){
      free(vme);
      return false;
    }

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    ofs += page_read_bytes;
    upage += PGSIZE;
  }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  struct page *kpage;
  bool success = false;

  //kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  kpage = alloc_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
  {
    struct vm_entry *vme;
    vme = (struct vm_entry *) malloc (sizeof(struct vm_entry));
    if (vme == NULL){ //malloc failure
      free_page(kpage);
      return false;
    }
    /* Set vm_entry members, offset and size to read when virtual page required
    zero bytes for padding at last, etc*/
    memset (vme, 0, sizeof (struct vm_entry));
    vme->type = VM_ANON;
    vme->writable = true;
    vme->is_loaded = true;
    vme->vaddr = ((uint8_t *) PHYS_BASE) - PGSIZE;
    /* insermt vm_entry. if fail, return false*/

    kpage->vme = vme;
    success = insert_vme(&kpage->thread->vm, vme);
    //kpage->thread = thread_current();
    //add_page_to_lru_list(kpage);
    success &= install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage->kaddr, true);
    if (success)
      *esp = PHYS_BASE;
      //*esp = PHYS_BASE - 12;
    else{
      free_page (kpage);
      free (vme);
    }
  }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* project3 - memory mapped file */
/* remove mapping of file from memory */ 
void do_munmap (struct mmap_file* map_file)
{
	struct list_elem *e;
	for (e = list_begin (&map_file->vme_list); e != list_end (&map_file->vme_list);)
	{
		struct vm_entry *vme = list_entry (e, struct vm_entry, mmap_elem);
    /* update disk */
    if (vme->is_loaded)
    {
      if (pagedir_is_dirty(thread_current()->pagedir, vme->vaddr))
      {
        lock_acquire(&filesys_lock);
        file_write_at(vme->file, vme->vaddr, vme->read_bytes, vme->offset);
        lock_release(&filesys_lock);
      }
      /* clean page table entry */
      void *kaddr; //physical addr
      kaddr = pagedir_get_page (thread_current()->pagedir, vme->vaddr);
      free_page(kaddr);
    }
    /* remove */
    vme->is_loaded = false;
    e = list_remove(e);
    delete_vme(&thread_current()->vm, vme); //containing free
	}
  list_remove(&map_file->elem);
  free(map_file);
}
