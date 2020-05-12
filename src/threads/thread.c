//latest modified file
#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* List of processes in THREAD_BLOCKED state, that is, processes
   that are blocked after the sleep() call  */
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* minimum wakeup_tick value of the threads in the sleep_list. */
static int64_t next_tick_to_wake = INT64_MAX;

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

/* For mlfqs  */
static int ready_threads;       /* # of ready threads + current threads */
static num_17_14 decay;         /* decay for reduce recent_cpu in thread. */
static num_17_14 load_avg;      /* average load on cpu due to ready thread. */

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */


/*if ticks is smaller than next_tick_to_wake,
update next_tick_to_wake value*/
void update_next_tick_to_wake(int64_t ticks)
{
  if(ticks<next_tick_to_wake)
      next_tick_to_wake = ticks;
}

/*return the next_tick_to_wake value*/
int64_t get_next_tick_to_wake(void)
{
  return next_tick_to_wake;
}

void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);
  load_avg = LOAD_AVG_DEFAULT;
  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

  /* save parent process */
  t->parent = thread_current();
  /* program not loaded & exited */
  t->load = false;
  t->exit = false;
  /* sema init to 0 */
  sema_init(&t->sema_exit , 0);
  sema_init(&t->sema_load , 0);
  /* add to child list */
  list_push_back(&(t->parent) -> child_list , &t -> childelem);

  /* init fd */
  t->fd_max = 2;
  t->fd_table = palloc_get_page(PAL_ZERO);
  /* Add to run queue. */
  thread_unblock (t);
  
  /* compare the priorities of the currently running thread 
     and the newly inserted one. Yield the CPU if the newly 
     arriving thread has higher priority*/
  struct thread *cur_t = thread_current();
  if(cur_t->priority < t->priority){
    thread_yield();
  }

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/*push current thread into sleep_list
and update to get the next tick to wake up the threads
then call thread_block*/
void
thread_sleep (int64_t ticks) 
{
  enum intr_level old_level;
  struct thread *cur = thread_current ();

  old_level = intr_disable ();
  if(cur != idle_thread){

    cur->wakeup_tick = ticks;
    list_push_back (&sleep_list, &cur->elem);
    /*update the ticks value for awake function to be performed*/
    update_next_tick_to_wake(ticks);
    thread_block();
  }
  intr_set_level (old_level);
}
/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);

  /*insert to ready list in order of priority*/
  list_insert_ordered(&ready_list, &t-> elem, cmp_priority, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}
/*look through the sleep_list and awake the threads
  which has bigger wakeup_tick value than the current tick
  and also update the next tick to wakeup the thread value*/
void
thread_awake (int64_t ticks) 
{
  struct list_elem* ent;

  next_tick_to_wake = INT64_MAX;
  ent = list_begin (&sleep_list);
  while (ent != list_end (&sleep_list)){
    struct thread *t= list_entry (ent, struct thread, elem);
    ent = list_next (ent);
    if (t->wakeup_tick <= ticks){
      list_remove (&t->elem);
      thread_unblock(t);
    }
    else{
      update_next_tick_to_wake(t->wakeup_tick);
    }
  }
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());
  struct thread *t = thread_current ();

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&t->allelem);
  /* tell that process exits to process descriptor  */ 
  /* parents process get out from block(using sema)*/
  t->exit = true;
  //palloc_free_page (t->fd_table);
  if(t!= initial_thread){
    sema_up(&(t->sema_exit));
  }
  t->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();

  /*when inserted to ready list, it follows priority order*/
  if (cur != idle_thread) 
    list_insert_ordered(&ready_list, &cur-> elem, cmp_priority, NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  if (!thread_mlfqs){
    thread_current ()->priority = new_priority;
    thread_current ()->init_priority = new_priority;
    
    /*considering priority donation*/
    refresh_priority();
    test_max_priority();
  }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/*compare the priority between current running thread
 and the highest priority except current thread
 If current thread has smaller priority, current thread yield*/
void test_max_priority (void){
  if (!list_empty (&ready_list)){
    struct list_elem *e;
    e=list_begin(&ready_list);
    struct thread *t = list_entry(e, struct thread, elem);
    if(thread_get_priority()<t->priority){
      thread_yield();
    }
  }
}

/* Compare the priority of two threads. */
bool cmp_priority (const struct list_elem *a,\
  const struct list_elem *b,void *aux UNUSED)
{
  struct thread *t_a = list_entry(a, struct thread, elem);
  struct thread *t_b = list_entry(b, struct thread, elem);
  if (t_a->priority > t_b->priority)
    return 1;
  return 0;
}

/*priority donation take place*/
void donate_priority(void)
{
  struct thread *cur = thread_current();
  int nested_depth_counter = 0;
  int donating_priority;

  /*donate priority to every nested threads*/
  while(cur->wait_on_lock){
    nested_depth_counter++;
    /*nested depth is limited to 8*/
    ASSERT(nested_depth_counter<=8);
    donating_priority = cur->priority;
    cur = cur->wait_on_lock->holder; /*there must exist holder*/
    if(cur->priority < donating_priority)
      cur->priority = donating_priority;
  }
}

/*when you release lock,
find and Remove every entries in the donations list*/
void remove_with_lock(struct lock *lock)
{
  struct list_elem* ent;
  struct thread *cur = thread_current();
  ent = list_begin (&cur->donations);
  while (ent != list_end (&cur->donations)){
    struct thread *t= list_entry (ent, struct thread, donation_elem);
    ent = list_next (ent);
    if (t->wait_on_lock == lock){
      list_remove (&t->donation_elem);
    }
  }
}

/*when thread priority changed, 
decide current thread's priority againconsider donation*/
void refresh_priority(void)
{
  struct list_elem* ent;
  struct thread *cur = thread_current();
  cur->priority = cur->init_priority;
  ent = list_begin (&cur->donations);
  while (ent != list_end (&cur->donations)){
    struct thread *t= list_entry (ent, struct thread, donation_elem);
    if (cur->priority < t->priority){
      cur->priority = t->priority;
    }
    ent = list_next (ent);
  }
}




/* Implementation for BSD */
/* Calculate priority using recent_cpu and nice on every 4th tick */
void 
mlfqs_priority (struct thread *t)
{
  /* Not yet implemented. --> Implemented */
  if(t != idle_thread){
    int priority= t->priority;
    int nice = t->nice; 
    num_17_14 recent_cpu = t->recent_cpu;

    priority = PRI_MAX - (recent_cpu / 4 / ONE_17_14) - (nice  * 2);
    t->priority = priority;
  }
}

/* Calculate recent_cpu every sec. */
void
mlfqs_recent_cpu (struct thread *t)
{
  /* Not yet implemented. --> Implemented */
  if(t != idle_thread){
    num_17_14 recent_cpu = t->recent_cpu;
    int nice = t->nice; 

    decay = ((int64_t) \
      (2 * load_avg) * ONE_17_14) / (2 * load_avg + ONE_17_14);
    recent_cpu = ((int64_t) \
      recent_cpu * decay / ONE_17_14 + nice * ONE_17_14);
    t->recent_cpu = recent_cpu;
  }
}

/* Calculate load_avg every sec. */
void
mlfqs_load_avg (void)
{
  /* Not yet implemented. --> Implemented */
  struct list_elem *e;
  int ready_thread_num = 0;
  for (e = list_begin (&ready_list); e != list_end (&ready_list);
       e = list_next (e))
    ready_thread_num++;
    
  if (thread_current () != idle_thread)
    ready_thread_num++;

  ready_threads = ready_thread_num;
  load_avg = load_avg * 59 / 60 + ready_threads * ONE_17_14 / 60;
  //printf("0x%x\n", load_avg);
  ASSERT (load_avg >= 0);
}

/* Increase recent_cpu by 1 by every timer interrupt. */
void
mlfqs_increment (void)
{
  /* Not yet implemented. --> Implemented */
  num_17_14 recent_cpu;
  if (thread_current () != idle_thread){
    recent_cpu = thread_current ()->recent_cpu;
    recent_cpu += ONE_17_14;
    //printf("recent:0x%x\n", recent_cpu);
    thread_current ()->recent_cpu = recent_cpu;
  }
}

/* Recaculate priority and recent_cpu for all threads. */
void 
mlfqs_recalc (void)
{
  /* Not yet implemented. --> Implemented */
  struct list_elem *e;
  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      mlfqs_recent_cpu (t);
      mlfqs_priority (t);
    }
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. --> done. */
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();

  cur -> nice = nice;
  mlfqs_priority(cur);

  /* Scheduling by priority. */
  struct list_elem *e;
  for (e = list_begin (&ready_list); e != list_end (&ready_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      if (t == cur) {
        list_remove(e);
        list_insert_ordered(&ready_list, &cur-> elem, cmp_priority, NULL);
      }
    }

  intr_set_level (old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. --> done */
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  int nice;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  nice = cur -> nice;
  intr_set_level (old_level);

  return nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented --> Implemented. */
  int load_avg_p;
  enum intr_level old_level;

  old_level = intr_disable ();
  load_avg_p = (load_avg * 100 + ONE_17_14 / 2) / ONE_17_14;
  intr_set_level (old_level);

  return load_avg_p;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented --> Implemented. */
  num_17_14 recent_cpu;
  int recent_cpu_p;
  enum intr_level old_level;

  old_level = intr_disable ();
  recent_cpu = thread_current ()->recent_cpu;
  recent_cpu_p = (int) ((recent_cpu * 100 + ONE_17_14 / 2) / ONE_17_14);
  intr_set_level (old_level);

  return recent_cpu_p;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;

  /* mlfq */
  t->nice = NICE_DEFAULT;
  t->recent_cpu = RECENT_CPU_DEFAULT;
  list_push_back (&all_list, &t->allelem);

  /*priority donation*/
  list_init(&t->donations);
  t->init_priority = priority;

  /* Initialize child list */
  list_init(&t -> child_list);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
    {
      ASSERT (prev != cur);
    }
} 

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
