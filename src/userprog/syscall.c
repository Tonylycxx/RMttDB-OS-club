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
  // printf ("system call!\n");
  int status = *(int *)f->esp;

  switch(status)
  {
    case SYS_EXIT:
      exit(*((int *)f->esp + 1));
      break;

    case SYS_WRITE:
      write(f, *((int *)(f->esp) + 1), (const void *)*((int *)(f->esp) + 2), (unsigned)*((int *)(f->esp) + 3));
      break;
    
    default:
      thread_exit ();
      break;
  }
}

void exit(int ret)
{
  thread_current()->ret_val = ret;
  thread_exit ();
}

int write(struct intr_frame *f, int fd, const void *buffer, unsigned size)
{
  if(fd == STDOUT_FILENO) 
  {
    putbuf((const char *)buffer, size);
    f->eax = size;
  }
  else
  {
    
  }
  
}
