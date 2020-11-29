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

bool is_valid_addr(const void *ptr_to_check)
{
  struct thread *cur = thread_current();
  if (!ptr_to_check || ptr_to_check <= 0x08048000 || !is_user_vaddr(ptr_to_check))
    return 0;
  else if (pagedir_get_page(cur->pagedir, ptr_to_check) == NULL)
    return 0;
  return 1;
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
    check_valid_addr(arg[0]);
    phys_page_ptr = pagedir_get_page(thread_current()->pagedir, arg[0]);
    if (!phys_page_ptr)
      exit(-1);
    phys_page_ptr = pagedir_get_page(thread_current()->pagedir, arg[0] + 1);
    if (!phys_page_ptr)
      exit(-1);
    exec(f, (char *)arg[0]);
    break;

  case SYS_WAIT:
    getargu(f->esp, &arg[0], 1);
    wait(f, (tid_t)arg[0]);
    break;

  case SYS_CREATE:
    getargu(f->esp, &arg[0], 2);
    check_valid_addr(arg[0]);
    phys_page_ptr = pagedir_get_page(thread_current()->pagedir, arg[0]);
    if (!phys_page_ptr)
      exit(-1);
    create(f, (char *)arg[0], (unsigned)arg[1]);
    break;

  case SYS_REMOVE:
    getargu(f->esp, &arg[0], 1);
    check_valid_addr(arg[0]);
    phys_page_ptr = pagedir_get_page(thread_current()->pagedir, arg[0]);
    if (!phys_page_ptr)
      exit(-1);
    remove(f, (char *)arg[0]);
    break;

  case SYS_OPEN:
    getargu(f->esp, &arg[0], 1);
    check_valid_addr(arg[0]);
    phys_page_ptr = pagedir_get_page(thread_current()->pagedir, arg[0]);
    if (!phys_page_ptr)
      exit(-1);
    open(f, (char *)arg[0]);
    break;

  case SYS_FILESIZE:
    getargu(f->esp, &arg[0], 1);
    filesize(f, (int)arg[0]);
    break;

  case SYS_READ:
    getargu(f->esp, &arg[0], 3);
    check_valid_addr(arg[1]);
    phys_page_ptr = pagedir_get_page(thread_current()->pagedir, arg[1]);
    if (!phys_page_ptr)
      exit(-1);
    read(f, (int)arg[0], (void *)arg[1], (unsigned)arg[2]);
    break;

  case SYS_WRITE:
    getargu(f->esp, &arg[0], 3);
    check_valid_addr(arg[1]);
    phys_page_ptr = pagedir_get_page(thread_current()->pagedir, arg[1]);
    if (!phys_page_ptr)
      exit(-1);
    write(f, (int)arg[0], (void *)arg[1], (unsigned)arg[2]);
    break;

  case SYS_SEEK:
    getargu(f->esp, &arg[0], 2);
    check_valid_addr(arg[1]);
    phys_page_ptr = pagedir_get_page(thread_current()->pagedir, arg[1]);
    if (!phys_page_ptr)
      exit(-1);
    seek(f, (int)arg[0], (unsigned)arg[1]);
    break;

  case SYS_TELL:
    getargu(f->esp, &arg[0], 1);
    check_valid_addr(arg[1]);
    phys_page_ptr = pagedir_get_page(thread_current()->pagedir, arg[1]);
    if (!phys_page_ptr)
      exit(-1);
    tell(f, (int)arg[0]);
    break;

  case SYS_CLOSE:
    getargu(f->esp, &arg[0], 1);
    close(f, (int)arg[0]);
    break;

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

void exec(struct intr_frame *f, const char *cmd_line)
{
  f->eax = process_execute(cmd_line);
}

void wait(struct intr_frame *f, tid_t tid)
{
  f->eax = process_wait(tid);
}

void create(struct intr_frame *f, const char *file, unsigned initial_size)
{
  acquire_file_lock();
  f->eax = filesys_create(file, initial_size);
  release_file_lock();
}

void remove(struct intr_frame *f, const char *file)
{
  acquire_file_lock();
  f->eax = filesys_remove(file);
  release_file_lock();
}

void open(struct intr_frame *f, const char *file_name)
{
  if (file_name == NULL)
  {
    f->eax = -1;
    return -1;
  }
  acquire_file_lock();
  struct file *file = filesys_open(file_name);
  release_file_lock();
  int fd;
  if (file == NULL)
  {
    f->eax = -1;
    return -1;
  }
  struct opened_file *op_file = malloc(sizeof(struct opened_file));
  op_file->f = file;
  fd = op_file->fd = thread_current()->cur_fd++;
  list_push_back(&thread_current()->opened_files, &op_file->elem);
  f->eax = fd;
  return fd;
}

void filesize(struct intr_frame *f, int fd)
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
    f->eax = file_length(op_file->f);
    release_file_lock();
  }
  else
    f->eax = -1;
}

void read(struct intr_frame *f, int fd, void *buffer, unsigned size)
{
  if (fd == STDIN_FILENO)
  {
    f->eax = (int)input_getc();
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

void write(struct intr_frame *f, int fd, const void *buffer, unsigned size)
{
  if (fd == STDOUT_FILENO)
  {
    putbuf((const char *)buffer, size);
    f->eax = size;
  }
  else if (fd == STDIN_FILENO)
  {
    f->eax = 0;
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

void seek(struct intr_frame *f, int fd, unsigned position)
{
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
  {
    return;
  }

  struct opened_file *op_file = get_op_file(fd);
  if (op_file && op_file->f)
  {
    acquire_file_lock();
    file_seek(op_file->f, position);
    release_file_lock();
  }
}

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
    file_tell(op_file->f);
    release_file_lock();
  }
}

void close(struct intr_frame *f, int fd)
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
    file_close(op_file->f);
    release_file_lock();
    list_remove(&op_file->elem);
  }
}