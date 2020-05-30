#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include <hash.h>
#include "threads/synch.h"
#include "filesys/file.h"
#include "vm/page.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
typedef int mapid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */
#define MAPID_ERROR ((mapid_t) -1)

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

/* 17.14 format properties. */
typedef int num_17_14;
#define ONE_17_14 ((num_17_14) 1 << 14)


/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list*/
    int64_t wakeup_tick;                /* tick for thread to wake up. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */
#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */

    /*For pj3 virtual memory*/
    struct hash vm; /* Hashtable for virtual address space that thread owns*/
    
    /*these are for priority donation*/
    int init_priority;               /* To initialize after donation */
    struct lock *wait_on_lock;       /* lock that current thread wait on*/
    struct list donations;           /* list of donators.*/
    struct list_elem donation_elem;  /* list element of list donations */

    /* Defined due to mlfqs. */
    int nice;                           /* Nice value of thread. */
    num_17_14 recent_cpu;               /* Usage of cpu in recent.  */  
 
 /* For pj2 - child/parent process connection */
    /* process hierarchy */
    struct thread *parent;           /*descriptor of parents process*/
    struct list_elem childelem;      /*element of child list*/
    struct list child_list;          /*child list*/

    bool load;                       /*existence of load*/
    bool exit;                       /*whether exit*/
    struct semaphore sema_exit;      /*exit semaphore*/
    struct semaphore sema_load;      /*load semaphore*/
    int exit_status;                 /*status of exit call*/
    /* file descriptor */
    struct file **fd_table;          /*file descriptor table*/
    int fd_max;                      /*maximum fd value exists on current table*/
    /* denying write to executable */
    struct file *cur_file;           /* current running file */

    /* pj3 - Memory mapped file */
    struct list mmap_list;          /* mmap list of opened file */
    mapid_t next_mapid;             /* first value is 0 for convinence */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

void update_next_tick_to_wake(int64_t ticks);
int64_t get_next_tick_to_wake(void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);
void thread_sleep (int64_t ticks);
void thread_awake (int64_t ticks);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);
void test_max_priority (void);
bool cmp_priority \
(const struct list_elem *a,const struct list_elem *b,void *aux UNUSED);

/* Priority */
void donate_priority(void);
void remove_with_lock(struct lock *lock);
void refresh_priority(void);


/* For BSD */
void mlfqs_priority (struct thread *t);
void mlfqs_recent_cpu (struct thread *t);
void mlfqs_load_avg (void);
void mlfqs_increment (void);
void mlfqs_recalc (void);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

#endif /* threads/thread.h */
