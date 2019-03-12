#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "devices/timer.h"
#include "lib/fixed-point.h"
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
//group21
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
static struct lock sleepers_lock; 

/* Implementing a list used to store the blocked processes in which we add processes to be blocked for a certain number of ticks
and removed when the wait time elapses*/
static struct list sleepers_list;


/* Idle thread. */
static struct thread *idle_thread;
int load_avg;
int time_counter = 0;
bool thread_mlfqs;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;
static int e_next_wakeup;
static struct thread *manager_thread; 
/* Lock used by thread_block_till(), which is used to gain exclusive 
   control over thsleepers_list. */
/* static struct lock sleepers_lock;    */
static void manager (void *aux UNUSED);
static void managerial_thread_work2 (void *aux UNUSED);


/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static struct thread *managerial_thread2;
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void schedule_tail (struct thread *prev);
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


/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
static bool th_before(const struct list_elem *a,const struct list_elem *b,void *aux UNUSED)
{
	return list_entry(a,struct thread,elem)->priority>list_entry(b,struct thread,elem)->priority; 
}

void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  //lock_init (&sleepers_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleepers_list); 
  /* next_wakeup_at = -1;  */
  //next_wakeup_at = INT64_MAX;
  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

void
thread_start (void) 
{
  e_next_wakeup=-100;
  load_avg = 0;
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
  thread_create ("manager", PRI_MAX, manager, NULL);
  thread_create("managerial_thread2", PRI_MAX, managerial_thread_work2, NULL);      /* Managerial thread is created in the starting when the thread_start is called*/

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

  uint64_t ticks = timer_ticks ();
  time_counter++;
   if(timer_ticks() == e_next_wakeup)
      thread_unblock (manager_thread);

  if(thread_mlfqs && timer_ticks() > 0)
  {
    mlfqs_increment ();
    if(timer_ticks() % 100 == 0)
    {
      if(managerial_thread2 && managerial_thread2->status == THREAD_BLOCKED)
        thread_unblock(managerial_thread2);
      // mlfqs_load_avg ();
      // mlfqs_recalculate ();
    }
    else if (timer_ticks() % 4 == 0)
    {
      //enum intr_level old_level = intr_disable();
      // mlfqs_priority (thread_current ());
      if(thread_current()->priority != PRI_MIN) thread_current()->priority = thread_current()->priority - 1;
      //intr_set_level(old_level);
    }
  }
  
  

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
  {
        intr_yield_on_return ();
  }
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

  /* Add to run queue. */
  thread_unblock (t);

  thread_check_for_yield();
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
  list_insert_ordered (&ready_list, &t->elem, th_before, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

static bool before(const struct list_elem *a,const struct list_elem *b,void *aux UNUSED)
{
	return list_entry(a,struct thread,elem)->wakeup_at < list_entry(b,struct thread,elem)->wakeup_at;
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

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it call schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
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
  if (cur != idle_thread) 
  {
	  list_insert_ordered (&ready_list, &cur->elem, th_before,NULL);
  }
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
  enum intr_level old_level = intr_disable ();
  struct thread *t = thread_current();
  int basePrio = t->priority; 
  t->oldp = t->priority; 
  t->priority = new_priority; 
  t->initial_priority = new_priority;
  thread_donate_priority(t);
  thread_check_for_yield();
  intr_set_level (old_level);
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  enum intr_level old_level = intr_disable ();
  int priority = thread_current ()->priority;
  intr_set_level (old_level);
  return priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  thread_current ()->nice = nice;
   
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  enum intr_level old_level = intr_disable ();
  int load_avg_nearest = convert_x_to_integer_nearest (multiply_x_by_n (load_avg, 100) );
  intr_set_level (old_level);
  return load_avg_nearest;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  enum intr_level old_level = intr_disable ();
  int recent_cpu_nearest = convert_x_to_integer_nearest (multiply_x_by_n (thread_current ()->recent_cpu, 100) );
  intr_set_level (old_level);
  return recent_cpu_nearest;
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

struct thread *
getChildThread (int id)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  struct thread *child = NULL;
  
  for (e = list_begin (&t->children);
       e!= list_end (&t->children); e = list_next (e))
  {
    struct thread *this = list_entry (e, struct thread, parentelement);
    if (this->tid == id)
    {
      child = this;
      break;
    }
  }
  return child;
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
  t->initial_priority = t->priority;
  //t->oldp=priority;
  t->executable= NULL;
  t->magic = THREAD_MAGIC;
  int i;
  for (i = 0; i<128; i++)
  {
    t->files[i] = NULL;
  }
  if (t != initial_thread)
  {
    t->parent = thread_current();
    list_push_back (&(t->parent->children), &t->parentelement);
  }
  else
    t->parent = NULL;
  list_init (&t->children);
  sema_init (&t->loaded, 0);
  sema_init (&t->almostexit, 0);
  t->exitstatus = -1;
  t->load = false;
  list_push_back (&all_list, &t->allelem);
  list_init (&t->locks_acquired); 
  t->lock_seeking = NULL;
  t->nice = 0;
  t->recent_cpu = 0;
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
  {
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
  }
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
schedule_tail (struct thread *prev) 
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
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until schedule_tail() has
   completed. */
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
  schedule_tail (prev); 
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

uint32_t thread_stack_ofs = offsetof (struct thread, stack);
void
thread_priority_restore()
{
    struct thread *t = thread_current ();
    //thread_set_priority(t->oldp);
    thread_current()->priority = thread_current()->oldp;

}
void
thread_priority_temporarily_up()
{
    struct thread *t = thread_current ();
    t->oldp = t->priority;
    //thread_set_priority(PRI_MAX);
    thread_current()->priority=PRI_MAX;
}
/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */


void thread_sleep(int64_t wakeup_at, int currentTime)
{
  // disabling the interrupts
	enum intr_level old_int=intr_disable();
  struct thread *th = thread_current();

  /* if the current time is greater than the time when it is supposed to wake up, then it doesn't have to sleep. */
  if(currentTime >= wakeup_at) return;
	
  ASSERT(th->status == THREAD_RUNNING); 
	th->wakeup_at = wakeup_at;       // setting the wakeup time of the thread.
	list_insert_ordered(&sleepers_list, &(th->elem), before, NULL);   // insert it to the sleeper list

  if(!list_empty(&sleepers_list))e_next_wakeup = list_entry(list_begin(&sleepers_list),struct thread,elem)->wakeup_at;
	
  thread_block();	
  //enabling the interrupts
	intr_set_level(old_int);
} 


void thread_check_for_yield(void)
{
  enum intr_level old_level = intr_disable();
  
  if(!list_empty(&ready_list))
  {
    struct list_elem * ready_head = list_front(&ready_list);
    struct thread *th = list_entry(ready_head, struct thread, elem); 
    if(th->priority > thread_current()->priority)
    {
      thread_yield();
    }
  }
  intr_set_level(old_level);
}


void update_ready_list(void)
{
  list_sort(&(ready_list), th_before, NULL);
}

void
thread_remove_lock (struct lock *lock)
{
  enum intr_level old_level = intr_disable ();
  list_remove (&lock->elem);
  thread_update_priority (thread_current ());
  intr_set_level (old_level);
}

void
thread_donate_priority (struct thread *t)
{
  enum intr_level old_level = intr_disable ();
  thread_update_priority (t);
  if (t->status == THREAD_READY)
    {
      update_ready_list();
    }
  intr_set_level (old_level);
}


bool
th_before2 (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct lock *la = list_entry(a, struct lock, elem),
            *lb = list_entry(b, struct lock, elem);
  return la->priority > lb->priority;
}

void
thread_update_priority (struct thread *t)
{
  enum intr_level old_level = intr_disable ();
  int max_priority = t->initial_priority;
  int lock_priority;
  if (!list_empty (&t->locks_acquired))
    {
      list_sort (&t->locks_acquired, th_before2, NULL);
      lock_priority = list_entry (list_front (&t->locks_acquired),
                                  struct lock, elem)->priority;
      if (lock_priority > max_priority)
        max_priority = lock_priority;
    }
  t->priority = max_priority;
  intr_set_level (old_level);
}

void
thread_add_lock (struct lock *lock)
{
  enum intr_level old_level = intr_disable ();
  list_insert_ordered (&thread_current ()->locks_acquired, &lock->elem, th_before2, NULL);
  if(!list_empty(&(lock->semaphore.waiters)))
  {
    int ma=-1;
    struct list_elem *e;
    for (e = list_begin(&(lock->semaphore.waiters)); e != list_end(&(lock->semaphore.waiters)); e = list_next (e)) 
    {
      if(ma < list_entry(e,struct thread,elem)->priority) ma = list_entry(e,struct thread,elem)->priority;
    }
    lock->priority = ma;
  }
  else lock->priority = 0;
  intr_set_level (old_level);
}

static void
manager(void *AUX) 
{
  manager_thread = thread_current ();
  
  while(true)
  {
    enum intr_level old_level = intr_disable();
    while(!list_empty(&sleepers_list))
    {
      struct thread * th2 = list_entry(list_begin(&sleepers_list),struct thread,elem);
      if(e_next_wakeup >= th2->wakeup_at)
      {
        list_pop_front(&sleepers_list);
        thread_unblock(th2);
      }
      else
        break;
    }
    if(!list_empty(&sleepers_list))
      e_next_wakeup = list_entry(list_begin(&sleepers_list),struct thread,elem)->wakeup_at;
    thread_block();            
    intr_set_level(old_level);   
  }
}


void 
mlfqs_increment (void)
{
  if (thread_current() == idle_thread || thread_current() == manager_thread || thread_current() == managerial_thread2) return;
  thread_current ()->recent_cpu = add_x_and_n (thread_current ()->recent_cpu, 1);
}

// load_avg = (59/60)*load_avg + (1/60)*ready_threads. 
void 
mlfqs_load_avg (void)
{
  int ready_threads = list_size (&ready_list);
  if(manager_thread && manager_thread->status == THREAD_READY)  ready_threads--;
  if(managerial_thread2 && managerial_thread2->status == THREAD_READY)  ready_threads--;
  if (thread_current() != idle_thread && thread_current() != manager_thread && thread_current() != managerial_thread2) ready_threads++;
  ASSERT(ready_threads >= 0)
  int term1 = divide_x_by_y (59, 60);
  term1 = multiply_x_by_y (term1, load_avg);
  int term2 = divide_x_by_y (ready_threads, 60);
  term1 = add_x_and_y (term1, term2);
  load_avg = term1;
  ASSERT (load_avg >= 0)
}

// recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice. 
void 
mlfqs_recent_cpu (struct thread *t)
{
  if (t == idle_thread || t == manager_thread || t == managerial_thread2) return;
  int term1 = multiply_x_by_n (2, load_avg);
  int term2 = term1 + convert_n_to_fixed_point (1);
  term1 = multiply_x_by_y (term1, t->recent_cpu);
  term1 = divide_x_by_y (term1, term2);
  term1 = add_x_and_n (term1, t->nice);
  t->recent_cpu = term1;
}

//  priority = PRI_MAX - (recent_cpu / 4) - (nice * 2). 
void 
mlfqs_priority (struct thread *t)
{
  if (t == idle_thread || t == manager_thread || t == managerial_thread2) return;
  int term1 = convert_n_to_fixed_point (PRI_MAX);
  int term2 = divide_x_by_n (t->recent_cpu, 4);
  int term3 = convert_n_to_fixed_point (multiply_x_by_n (t->nice, 2));
  term1 = substract_y_from_x (term1, term2);
  term1 = substract_y_from_x (term1, term3);
  term1 = convert_x_to_integer_zero (term1);
  if (term1 < PRI_MIN) t->priority = PRI_MIN;
  else if (term1 > PRI_MAX) t->priority = PRI_MAX;
  else  t->priority = term1;
}

void 
mlfqs_recalculate (void)
{
  struct list_elem *e;
  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e))
    {
      if(e == managerial_thread2 || e == manager_thread || e == idle_thread)
      {
        continue;
      }
      struct thread *t = list_entry (e, struct thread, allelem);
      mlfqs_recent_cpu (t);
      mlfqs_priority (t);
    }
  update_ready_list();
}



static void
managerial_thread_work2(void *AUX)
{
  managerial_thread2 = thread_current();
  while(true)
  { 
    enum intr_level old_level = intr_disable();  
      mlfqs_recalculate ();
    mlfqs_load_avg ();
    thread_block();
    intr_set_level(old_level);
  }
}

void
thread_wakeup (int64_t current_tick)
{
  if(!list_empty(&sleepers_list)) 
  {
    struct thread * th = list_entry(list_begin(&sleepers_list), struct thread, elem); 
    if(th->wakeup_at <= current_tick)     
    {
      list_pop_front(&sleepers_list);
      thread_unblock(th);
      intr_yield_on_return();               
    }
  }

  return;
}
