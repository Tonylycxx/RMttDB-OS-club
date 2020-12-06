#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "devices/input.h"
#include "devices/shutdown.h"

static void syscall_handler(struct intr_frame *);

static void halt(void);
static void exit(int);
static void exec(struct intr_frame *f, const char *cmd_line);
static void wait(struct intr_frame *f, tid_t tid);
static void create(struct intr_frame *f, const char *file, unsigned initial_size);
static void remove(struct intr_frame *f, const char *file);
static void open(struct intr_frame *f, const char *file_name);
static void filesize(struct intr_frame *f, int fd);
static void read(struct intr_frame *f, int fd, char *buffer, unsigned size);
static void write(struct intr_frame *f, int fd, const char *buffer, unsigned size);
static void seek(int fd, unsigned position);
static void tell(struct intr_frame *f, int fd);
static void close(int);

/* Initate system call system, bound system call to interrupt 0.30 */
void syscall_init(void)
{
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Check pointer's validation
   Firstly check whether it points to user zone
   Secondly check whether pages it points to is valid. */
void check_valid_addr(const void *ptr_to_check)
{
  struct thread *cur = thread_current();
  if (!ptr_to_check || (void *)ptr_to_check <= 0x08048000 || !is_user_vaddr(ptr_to_check))
    exit(-1);
  else if (pagedir_get_page(cur->pagedir, ptr_to_check) == NULL)
    exit(-1);
}

/* When a argument points to a string, use this function
   to check each char in order to garuntee the safty. */
void check_valid_argu(char *ptr_to_check)
{
  for (;;)
  {
    check_valid_addr(ptr_to_check);
    if (*ptr_to_check == '\0')
      return;
    ptr_to_check++;
  }
}

/* Get first 'argument_number' arguments and save in array arg. */
void getargu(void *esp, int *arg, int argument_number)
{
  int i;
  for (i = 0; i < argument_number; i++)
  {
    int *res = (int *)esp + i + 1;
    check_valid_addr(res + 1);
    arg[i] = *res;
  }
}

/* Find certain file opened by current thread and return a struct. */
struct opened_file *get_op_file(int fd)
{
  struct list_elem *e;
  for (e = list_begin(&thread_current()->opened_files); e != list_end(&thread_current()->opened_files);
       e = list_next(e))
  {
    struct opened_file *op_file = list_entry(e, struct opened_file, elem);
    if (op_file->fd == fd)
    {
      return op_file;
    }
  }
  return NULL;
}

/* Syscall handler, which call corresponding function to handle
   different system calls. */
static void
syscall_handler(struct intr_frame *f)
{
  check_valid_addr(f->esp);
  check_valid_addr(f->esp + 1);

  int status = *(int *)f->esp;
  int arg[3];

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
    check_valid_argu((char *)arg[0]);
    exec(f, (char *)arg[0]);
    break;

  case SYS_WAIT:
    getargu(f->esp, &arg[0], 1);
    wait(f, (tid_t)arg[0]);
    break;

  case SYS_CREATE:
    getargu(f->esp, &arg[0], 2);
    check_valid_argu((char *)arg[0]);
    create(f, (char *)arg[0], (unsigned)arg[1]);
    break;

  case SYS_REMOVE:
    getargu(f->esp, &arg[0], 1);
    check_valid_argu((char *)arg[0]);
    remove(f, (char *)arg[0]);
    break;

  case SYS_OPEN:
    getargu(f->esp, &arg[0], 1);
    check_valid_argu((char *)arg[0]);
    open(f, (char *)arg[0]);
    break;

  case SYS_FILESIZE:
    getargu(f->esp, &arg[0], 1);
    filesize(f, (int)arg[0]);
    break;

  case SYS_READ:
    getargu(f->esp, &arg[0], 3);
    check_valid_argu((char *)arg[1]);
    read(f, (int)arg[0], (char *)arg[1], (unsigned)arg[2]);
    break;

  case SYS_WRITE:
    getargu(f->esp, &arg[0], 3);
    check_valid_argu((char *)arg[1]);
    write(f, (int)arg[0], (char *)arg[1], (unsigned)arg[2]);
    break;

  case SYS_SEEK:
    getargu(f->esp, &arg[0], 2);
    seek((int)arg[0], (unsigned)arg[1]);
    break;

  case SYS_TELL:
    getargu(f->esp, &arg[0], 1);
    tell(f, (int)arg[0]);
    break;

  case SYS_CLOSE:
    getargu(f->esp, &arg[0], 1);
    close((int)arg[0]);
    break;

  default:
    exit(-1);
    break;
  }
}

/* Some system calls downwards needed file system lock. */

/* System handler function for halt() */
void halt(void)
{
  shutdown_power_off();
}

/* System handler function for exit() */
void exit(int status)
{
  thread_current()->ret_val = status;
  thread_exit();
}

/* System handler function for exec() */
void exec(struct intr_frame *f, const char *cmd_line)
{
  f->eax = process_execute(cmd_line);
}

/* System handler function for wait() */
void wait(struct intr_frame *f, tid_t tid)
{
  f->eax = process_wait(tid);
}

/* System handler function for create() */
void create(struct intr_frame *f, const char *file, unsigned initial_size)
{
  acquire_file_lock();
  f->eax = filesys_create(file, initial_size);
  release_file_lock();
}

/* System handler function for remove() */
void remove(struct intr_frame *f, const char *file)
{
  acquire_file_lock();
  f->eax = filesys_remove(file);
  release_file_lock();
}

/* System handler function for open() */
void open(struct intr_frame *f, const char *file_name)
{
  if (file_name == NULL)
  {
    f->eax = -1;
    return;
  }
  acquire_file_lock();
  struct file *file = filesys_open(file_name);
  release_file_lock();
  int fd;
  if (file == NULL)
  {
    f->eax = -1;
    return;
  }
  struct opened_file *op_file = malloc(sizeof(struct opened_file));
  op_file->f = file;
  fd = op_file->fd = thread_current()->cur_fd++;
  list_push_back(&thread_current()->opened_files, &op_file->elem);
  f->eax = fd;
}

/* System handler function for filesize() */
void filesize(struct intr_frame *f, int fd)
{
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
  {
    f->eax = 0;
    return;
  }
  struct opened_file *op_file = get_op_file(fd);
  if (op_file && op_file->f)
  {
    acquire_file_lock();
    f->eax = file_length(op_file->f);
    release_file_lock();
  }
  else
    f->eax = -1;
}

/* System handler function for read() */
void read(struct intr_frame *f, int fd, char *buffer, unsigned size)
{
  if (fd == STDIN_FILENO)
  {
    unsigned i;
    for (i = 0; i < size; i++)
      buffer[i] = input_getc();
    f->eax = size;
    return;
  }
  else if (fd == STDOUT_FILENO)
  {
    f->eax = 0;
    return;
  }
  struct opened_file *op_file = get_op_file(fd);
  if (op_file && op_file->f)
  {
    acquire_file_lock();
    f->eax = file_read(op_file->f, buffer, size);
    release_file_lock();
  }
  else
    f->eax = -1;
}

/* System handler function for write() */
void write(struct intr_frame *f, int fd, const char *buffer, unsigned size)
{
  if (fd == STDOUT_FILENO)
  {
    putbuf((const char *)buffer, size);
    f->eax = size;
    return;
  }
  struct opened_file *op_file = get_op_file(fd);
  if (op_file && op_file->f)
  {
    acquire_file_lock();
    f->eax = file_write(op_file->f, buffer, size);
    release_file_lock();
  }
  else
    f->eax = 0;
}

/* System handler function for seek() */
void seek(int fd, unsigned position)
{
  struct opened_file *op_file = get_op_file(fd);
  if (op_file && op_file->f)
  {
    acquire_file_lock();
    file_seek(op_file->f, position);
    release_file_lock();
  }
}

/* System handler function for tell() */
void tell(struct intr_frame *f, int fd)
{
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
  {
    f->eax = -1;
    return;
  }
  struct opened_file *op_file = get_op_file(fd);
  if (op_file && op_file->f)
  {
    acquire_file_lock();
    f->eax = file_tell(op_file->f);
    release_file_lock();
  }
  else
    f->eax = -1;
}

/* System handler function for close() */
void close(int fd)
{
  struct opened_file *op_file = get_op_file(fd);
  if (op_file && op_file->f)
  {
    acquire_file_lock();
    file_close(op_file->f);
    release_file_lock();
    list_remove(&op_file->elem);
    free(op_file);
  }
}