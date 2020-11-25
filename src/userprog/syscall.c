#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  printf ("system call!\n");
  int status = *(int *)f->esp;

  switch(status)
  {
    case SYS_EXIT:
      thread_current()->ret_val = *((int *)f->esp + 1);
      thread_exit ();
      break;
    
    default:
      thread_exit ();
      break;
  }

}
