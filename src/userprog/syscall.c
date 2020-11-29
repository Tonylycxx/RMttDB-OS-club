#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/malloc.h"

static void syscall_handler(struct intr_frame *);

void syscall_init(void)
{
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void check_valid_addr(const void *ptr_to_check)
{
  struct thread *cur = thread_current();
  if (!ptr_to_check || ptr_to_check <= 0x08048000 || !is_user_vaddr(ptr_to_check))
    exit(-1);
  else if (pagedir_get_page(cur->pagedir, ptr_to_check) == NULL)
    exit(-1);
}

void getargu(void *esp, int *arg, int argument_index)
{
  int i;
  for (i = 0; i < argument_index; i++)
  {
    int *res = (int *)esp + i + 1;
    check_valid_addr(res + 1);
    arg[i] = *res;
  }
}
static void
syscall_handler(struct intr_frame *f)
{
  check_valid_addr(f->esp);
  check_valid_addr(f->esp + 1);

  int status = *(int *)f->esp;
  int arg[3];
  void *phys_page_ptr = NULL;

  switch (status)
  {
  case SYS_HALT:
    halt();
    break;

  case SYS_EXIT:
    getargu(f->esp, &arg[0], 1);
    exit(arg[0]);
    break;

  case SYS_EXEC:
    getargu(f->esp, &arg[0], 1);
    phys_page_ptr = pagedir_get_page(thread_current()->pagedir, arg[0]);
    if(!phys_page_ptr)
      exit(-1);
    arg[0] = phys_page_ptr;
    exec(f, (char *)arg[0]);
    break;

  case SYS_WAIT:
    getargu(f->esp, &arg[0], 1);
    wait(f, (tid_t)arg[0]);
    break;

    // case SYS_CREATE:
    //   create ((char *)(*getargu(f->esp, 0)), (unsigned)(*getargu(f->esp, 1)));
    //   break;

    // case SYS_REMOVE:
    //   remove ((char *)(*getargu(f->esp, 0)));
    //   break;

  case SYS_OPEN:
    //f->eax = ((char *)(*getargu(f->esp, 0)));
    getargu(f->esp, &arg[0], 1);
    phys_page_ptr = pagedir_get_page(thread_current()->pagedir, arg[0]);
    if(!phys_page_ptr)
      exit(-1);
    arg[0] = phys_page_ptr;
    open(f, (char *)arg[0]);
    break;

    // case SYS_FILESIZE:
    //   filesize ((int)(*getargu(f->esp, 0)));
    //   break;

    // case SYS_READ:
    //   read ((int)(*getargu(f->esp, 0)), (void *)(*getargu(f->esp, 1)), (unsigned)(*getargu(f->esp, 2)));
    //   break;

  case SYS_WRITE:
    getargu(f->esp, &arg[0], 3);
    phys_page_ptr = pagedir_get_page(thread_current()->pagedir, arg[1]);
    if(!phys_page_ptr)
      exit(-1);
    arg[1] = phys_page_ptr;
    write(f, (int)arg[0], (void *)arg[1], (unsigned)arg[2]);
    break;

    // case SYS_SEEK:
    //   seek ((int)(*getargu(f->esp, 0)), (unsigned)(*getargu(f->esp, 1)));
    //   break;

    // case SYS_TELL:
    //   tell ((int)(*getargu(f->esp, 0)));
    //   break;

    // case SYS_CLOSE:
    //   close ((int)(*getargu(f->esp, 0)));
    //   break;

  default:
    exit(-1);
    break;
  }
}

void halt(void)
{
  shutdown_power_off();
}

void exit(int status)
{
  thread_current()->ret_val = status;
  thread_exit();
}

tid_t exec(struct intr_frame *f, const char *cmd_line)
{
  f->eax = process_execute(cmd_line);
}

int wait(struct intr_frame *f, tid_t tid)
{
  f->eax = process_wait(tid);
}

// bool
// create (const char *file, unsigned initial_size)
// {

// }

// bool
// remove (const char *file)
// {

// }

int open(struct intr_frame *f, const char *file_name)
{
  if (file_name == NULL)
  {
    f->eax = -1;
    exit(-1);
  }
  struct file *file = filesys_open(file_name);
  int fd;
  if (file == NULL)
  {
    f->eax = -1;
    exit(-1);
  }
  struct opened_file *op_file = malloc(sizeof(struct opened_file));
  op_file->f = file;
  fd = op_file->fd = thread_current()->cur_fd++;
  list_push_back(&thread_current()->openend_files, &op_file->elem);
  f->eax = fd;
}

int filesize(int fd)
{
}

// int
// read (int fd, void *buffer, unsigned size)
// {

// }

int write(struct intr_frame *f, int fd, const void *buffer, unsigned size)
{
  if (fd == STDOUT_FILENO)
  {
    putbuf((const char *)buffer, size);
    f->eax = size;
  }
}

// void
// seek (int fd, unsigned position)
// {

// }

// unsigned
// tell (int fd)
// {

// }

// void
// close (int fd)
// {

// }