#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "filesys/file.h"
#include "userprog/syscall.h"
#include "threads/synch.h"

/* States in a thread's life cycle. */
enum thread_status
{
   THREAD_RUNNING, /* Running thread. */
   THREAD_READY,   /* Not running but ready to run. */
   THREAD_BLOCKED, /* Waiting for an event to trigger. */
   THREAD_DYING    /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t)-1) /* Error value for tid_t. */

typedef int mapid_t;

/* Thread priorities. */
#define PRI_MIN 0      /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63     /* Highest priority. */

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
   tid_t tid;                 /* Thread identifier. */
   enum thread_status status; /* Thread state. */
   char name[16];             /* Name (for debugging purposes). */
   uint8_t *stack;            /* Saved stack pointer. */
   int priority;              /* Priority. */
   struct list_elem allelem;  /* List element for all threads list. */

   /* Shared between thread.c and synch.c. */
   struct list_elem elem; /* List element. */

   int64_t blocked_ticks; /* Number of ticks thread has blocked */

   int ret_val;                  /* Thread's return value, used in syscall exit() */
   struct semaphore wait_child;  /* A semaphore used to synchronize with parent thread, in order to transmit information during creating a new thread */
   struct thread *parent_thread; /* Pointer points to parent thread, used to synchronize with parent thread */
   struct list child_list;       /* A list includes all the child threads */

   struct list opened_files; /* A list includes all thread's opened filed */
   int cur_fd;               /* Next fd allocate to next open file */
   struct file *this_file;   /* Pointer points to the executable file current process running */
   bool load_result;         /* A boolean variable used to make sure whether a child process is successfully loaded */

   struct hash *page_table;      /* Supplemental page table, used to store thread's pages' infomation */
   void *esp;                    /* Current thread's esp, used to handle page-fault */
   struct list mmap_file_list;   /* Thread's memory-mapped files list */
   mapid_t next_mapid;           /* Next mapid current thread can allocate */

#ifdef USERPROG
   /* Owned by userprog/process.c. */
   uint32_t *pagedir; /* Page directory. */
#endif

   /* Owned by thread.c. */
   unsigned magic; /* Detects stack overflow. */
};

/* Struct to save child processes infomation */
struct saved_child
{
   tid_t tid;             /* This thread's tid */
   int ret_val;           /* This thread's return value used in syscall exit() */
   struct list_elem elem; /* List elem to make a list */
   struct semaphore sema; /* Semaphore used in syscall wait() to wait a certain child process(thread) */
};

/* Struct to save a thread's opened files */
struct opened_file
{
   struct file *f;        /* Pointer points to a file */
   int fd;                /* This file's fd (might be different according to process) */
   struct list_elem elem; /* List elem to make a list */
};

struct mmap_handler
{
   mapid_t mapid;             /* Mapid */
   struct file *mmap_file;    /* Mapped file */
   void *mmap_addr;           /* Address file mapped to */
   int num_page;              /* Number of pages used in mapping the file */
   int last_page_size;        /* Usage of the last page */
   struct list_elem elem;     /* List_elem used to keep a list */
   bool writable;             /* Is writable or not */
   bool is_segment;           /* Is a segment or not */
   bool is_static_data;       /* Is static or not */
   int num_page_with_segment; /* Segment's count of pages */
   off_t file_ofs;            /* Mapping offset of file */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func(struct thread *t, void *aux);
void thread_foreach(thread_action_func *, void *);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

void thread_blocked_check(struct thread *t, void *aux UNUSED);

void acquire_file_lock(void);
void release_file_lock(void);

void save_wait_info(void);
void free_children(void);
void free_opened_files(void);

#endif /* threads/thread.h */
