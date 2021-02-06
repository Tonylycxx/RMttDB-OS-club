#include <stdio.h>
#include <string.h>
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
#include "lib/user/syscall.h"
#include "userprog/syscall.h"

static void syscall_handler(struct intr_frame *);

static void syscall_halt(void);
static void syscall_exit(int);
static void syscall_exec(struct intr_frame *f, const char *cmd_line);
static void syscall_wait(struct intr_frame *f, tid_t tid);
static void syscall_create(struct intr_frame *f, const char *file, unsigned initial_size);
static void syscall_remove(struct intr_frame *f, const char *file);
static void syscall_open(struct intr_frame *f, const char *file_name);
static void syscall_filesize(struct intr_frame *f, int fd);
static void syscall_read(struct intr_frame *f, int fd, char *buffer, unsigned size);
static void syscall_write(struct intr_frame *f, int fd, const char *buffer, unsigned size);
static void syscall_seek(int fd, unsigned position);
static void syscall_tell(struct intr_frame *f, int fd);
static void syscall_close(int);

/* Initate system call system, bound system call to interrupt 0.30 */
void syscall_init(void)
{
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void thread_exit_with_retval(struct intr_frame *f, int ret_val)
{
  struct thread *cur = thread_current();
  cur->ret_val = ret_val;
  f->eax = (uint32_t)ret_val;
  thread_exit();
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

bool syscall_check_user_vaddr(const void *vaddr, bool write)
{
  if (vaddr == NULL || !is_user_vaddr(vaddr) || vaddr <= 0x08048000)
    return false;

#ifdef VM
  struct page_table_elem *base = page_find_with_lock(thread_current()->page_table, pg_round_down(vaddr));
  if (base == NULL)
    return page_pagefault_handler(vaddr, write, thread_current()->esp);
  else
    return !(write && !(base->writable));
#else
  ASSERT(vaddr != NULL);
  return pagedir_get_page(thread_current()->pagedir, vaddr) != NULL;
#endif
}

bool syscall_check_user_string(const char *ustr)
{
  if (!syscall_check_user_vaddr(ustr, false))
    return false;
  while (*ustr != '\0')
  {
    ustr++;
    if (((int)ustr & PGMASK) == 0)
    {
      if (!syscall_check_user_vaddr(ustr, false))
        return false;
    }
  }
  return true;
}

bool syscall_check_user_buffer(const char *ustr, int size, bool write)
{
  if (!syscall_check_user_vaddr(ustr + size - 1, write))
    return false;
  size >>= 12;
  do
  {
    if (!syscall_check_user_vaddr(ustr, write))
      return false;
    ustr += 1 << 12;
  } while (size--);
  return true;
}

/* Syscall handler, which call corresponding function to handle
   different system calls. */
static void
syscall_handler(struct intr_frame *f)
{

#ifdef VM
  thread_current()->esp = f->esp;
#endif
  if (!syscall_check_user_buffer(f->esp, 4, false))
    thread_exit_with_retval(f, -1);

  int status = *(int *)f->esp;
  void *arg1 = f->esp + 4, *arg2 = f->esp + 8, *arg3 = f->esp + 12;

  switch (status)
  {
  case SYS_EXIT:
  case SYS_EXEC:
  case SYS_WAIT:
  case SYS_TELL:
  case SYS_CLOSE:
  case SYS_REMOVE:
  case SYS_OPEN:
  case SYS_FILESIZE:

#ifdef VM
  case SYS_MUNMAP:
#endif

#ifdef FILESYS
  case SYS_CHDIR:
  case SYS_MKDIR:
  case SYS_ISDIR:
  case SYS_INUMBER:
#endif

    if (!syscall_check_user_buffer(arg1, 4, false))
      thread_exit_with_retval(f, -1);
    break;

  case SYS_CREATE:
  case SYS_SEEK:

#ifdef VM
  case SYS_MMAP:
#endif

#ifdef FILESYS
  case SYS_READDIR:
#endif

    if (!syscall_check_user_buffer(arg1, 8, false))
      thread_exit_with_retval(f, -1);
    break;

  case SYS_READ:
  case SYS_WRITE:
    if (!syscall_check_user_buffer(arg1, 12, false))
      thread_exit_with_retval(f, -1);
    break;

  default:
    break;
  }

  switch (status)
  {
  case SYS_HALT:
    syscall_halt();
    break;

  case SYS_EXIT:
    syscall_exit(*((int *)arg1));
    break;

  case SYS_EXEC:
    syscall_exec(f, *((char **)arg1));
    break;

  case SYS_WAIT:
    syscall_wait(f, *((tid_t *)arg1));
    break;

  case SYS_CREATE:
    syscall_create(f, *((char **)arg1), *((unsigned *)arg2));
    break;

  case SYS_REMOVE:
    syscall_remove(f, *((char **)arg1));
    break;

  case SYS_OPEN:
    syscall_open(f, *((char **)arg1));
    break;

  case SYS_FILESIZE:
    syscall_filesize(f, *((int *)arg1));
    break;

  case SYS_READ:
    syscall_read(f, *((int *)arg1), *((char **)arg2), *((unsigned *)arg3));
    break;

  case SYS_WRITE:
    syscall_write(f, *((int *)arg1), *((char **)arg2), *((unsigned *)arg3));
    break;

  case SYS_SEEK:
    syscall_seek(*((int *)arg1), *((unsigned *)arg2));
    break;

  case SYS_TELL:
    syscall_tell(f, *((int *)arg1));
    break;

  case SYS_CLOSE:
    syscall_close(*((int *)arg1));
    break;

  case SYS_MMAP:
    syscall_mmap(f, *((int *)arg1), arg2);
    break;

  case SYS_MUNMAP:
    syscall_munmap(f, *((int *)arg1));
    break;

  default:
    thread_exit_with_retval(f, -1);
    break;
  }
}

/* Some system calls downwards needed file system lock. */

/* System handler function for halt() */
void syscall_halt(void)
{
  shutdown_power_off();
}

/* System handler function for exit() */
void syscall_exit(int status)
{
  thread_current()->ret_val = status;
  thread_exit();
}

/* System handler function for exec() */
void syscall_exec(struct intr_frame *f, const char *cmd_line)
{
  if (!syscall_check_user_string(cmd_line))
    thread_exit_with_retval(f, -1);
  f->eax = process_execute(cmd_line);
}

/* System handler function for wait() */
void syscall_wait(struct intr_frame *f, tid_t tid)
{
  f->eax = process_wait(tid);
}

/* System handler function for create() */
void syscall_create(struct intr_frame *f, const char *file, unsigned initial_size)
{
  if (!syscall_check_user_string(file))
    thread_exit_with_retval(f, -1);
  acquire_file_lock();
  f->eax = filesys_create(file, initial_size);
  release_file_lock();
}

/* System handler function for remove() */
void syscall_remove(struct intr_frame *f, const char *file)
{
  if (!syscall_check_user_string(file))
    thread_exit_with_retval(f, -1);
  acquire_file_lock();
  f->eax = filesys_remove(file);
  release_file_lock();
}

/* System handler function for open() */
void syscall_open(struct intr_frame *f, const char *file_name)
{
  if (file_name == NULL || !syscall_check_user_string(file_name))
  {
    thread_exit_with_retval(f, -1);
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
void syscall_filesize(struct intr_frame *f, int fd)
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
void syscall_read(struct intr_frame *f, int fd, char *buffer, unsigned size)
{
  if (!syscall_check_user_buffer(buffer, size, true))
    thread_exit_with_retval(f, -1);
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
void syscall_write(struct intr_frame *f, int fd, const char *buffer, unsigned size)
{
  if (!syscall_check_user_buffer(buffer, size, false))
    thread_exit_with_retval(f, -1);
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
void syscall_seek(int fd, unsigned position)
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
void syscall_tell(struct intr_frame *f, int fd)
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
void syscall_close(int fd)
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

bool mmap_check_mmap_vaddr(struct thread *cur, const void *vaddr, int num_page)
{
#ifdef VM
  bool res = true;
  int i;
  for (i = 0; i < num_page; i++)
    if (!page_available_upage(cur->page_table, vaddr + i * PGSIZE))
      res = false;
  return res;
#endif
}

bool mmap_install_page(struct thread *cur, struct mmap_handler *mh)
{
#ifdef VM
  bool res = true;
  int i;
  for (i = 0; i < mh->num_page; i++)
    if (!page_install_file(cur->page_table, mh, mh->mmap_addr + i * PGSIZE))
      res = false;
  if (mh->is_segment)
    for (i = mh->num_page; i < mh->num_page_with_segment; i++)
      if (!page_install_file(cur->page_table, mh, mh->mmap_addr + i * PGSIZE))
        res = false;
  return res;
#endif
}

void mmap_read_file(struct mmap_handler *mh, void *upage, void *kpage)
{
#ifdef VM
  if (mh->is_segment)
  {
    void *addr = mh->mmap_addr + mh->num_page * PGSIZE + mh->last_page_size;
    if (mh->last_page_size != 0)
      addr -= PGSIZE;
    if (addr > upage)
    {
      if (addr - upage < PGSIZE)
      {
        file_read_at(mh->mmap_file, kpage, mh->last_page_size, upage - mh->mmap_addr + mh->file_ofs);
        memset(kpage + mh->last_page_size, 0, PGSIZE - mh->last_page_size);
      }
      else
        file_read_at(mh->mmap_file, kpage, PGSIZE, upage - mh->mmap_addr + mh->file_ofs);
    }
    else
      memset(kpage, 0, PGSIZE);
  }
  else
  {
    if (mh->mmap_addr + file_length(mh->mmap_file) - upage < PGSIZE)
    {
      file_read_at(mh->mmap_file, kpage, mh->last_page_size, upage - mh->mmap_addr + mh->file_ofs);
      memset(kpage + mh->last_page_size, 0, PGSIZE - mh->last_page_size);
    }
    else
      file_read_at(mh->mmap_file, kpage, PGSIZE, upage - mh->mmap_addr + mh->file_ofs);
  }
#endif
}

void mmap_write_file(struct mmap_handler *mh, void *upage, void *kpage)
{
#ifdef VM
  if (mh->writable)
  {
    if (mh->is_segment)
    {
      void *addr = mh->mmap_addr + mh->num_page * PGSIZE + mh->last_page_size;
      if (addr > upage)
      {
        if (addr - upage < PGSIZE)
          file_write_at(mh->mmap_file, kpage, mh->last_page_size, upage - mh->mmap_addr + mh->file_ofs);
        else
          file_write_at(mh->mmap_file, kpage, PGSIZE, upage - mh->mmap_addr + mh->file_ofs);
      }
    }
    else
    {
      if (mh->mmap_addr + file_length(mh->mmap_file) - upage < PGSIZE)
        file_write_at(mh->mmap_file, kpage, mh->last_page_size, upage - mh->mmap_addr + mh->file_ofs);
      else
        file_write_at(mh->mmap_file, kpage, PGSIZE, upage - mh->mmap_addr + mh->file_ofs);
    }
  }
#endif
}

bool mmap_load_segment(struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
#ifdef VM
  ASSERT(!((read_bytes + zero_bytes) & PGMASK));
  struct thread *cur = thread_current();
  mapid_t mapid = cur->next_mapid++;
  struct mmap_handler *mh = (struct mmap_handler *)malloc(sizeof(struct mmap_handler));
  mh->mapid = mapid;
  mh->mmap_file = file;
  mh->writable = writable;
  mh->is_static_data = writable;
  int num_page = read_bytes / PGSIZE;
  int total_num_page = ((read_bytes + zero_bytes) / PGSIZE);
  int last_page_used = read_bytes & PGMASK;
  if (last_page_used != 0)
    num_page++;
  if (!mmap_check_mmap_vaddr(cur, upage, total_num_page))
    return false;
  mh->mmap_addr = upage;
  mh->num_page = num_page;
  mh->last_page_size = last_page_used;
  mh->num_page_with_segment = total_num_page;
  mh->is_segment = true;
  mh->file_ofs = ofs;
  list_push_back(&cur->mmap_file_list, &mh->elem);
  if (!mmap_install_page(cur, mh))
    return false;
  return true;
#endif
}

void syscall_mmap(struct intr_frame *f, int fd, const void *obj_vaddr)
{
#ifdef VM
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
  {
    f->eax = MAP_FAILED;
    return;
  }
  if (obj_vaddr == NULL || ((uint32_t)obj_vaddr % (uint32_t)PGSIZE != 0))
  {
    f->eax = MAP_FAILED;
    return;
  }

  struct thread *cur = thread_current();
  struct opened_file *of = get_op_file(fd);

  if (of != NULL)
  {
    mapid_t mapid = cur->next_mapid++;
    struct mmap_handler *mh = (struct mmap_handler *)malloc(sizeof(struct mmap_handler));
    mh->mapid = mapid;
    mh->mmap_file = file_reopen(of->f);
    mh->writable = true;
    mh->is_segment = false;
    mh->is_static_data = false;
    mh->file_ofs = 0;
    off_t file_size = file_length(mh->mmap_file);
    int num_page = file_size / PGSIZE;
    int last_page_used = file_size % PGSIZE;
    if (last_page_used != 0)
      num_page++;
    if (!mmap_check_mmap_vaddr(cur, obj_vaddr, num_page))
    {
      f->eax = MAP_FAILED;
      return;
    }
    mh->mmap_addr = obj_vaddr;
    mh->num_page = num_page;
    mh->num_page_with_segment = num_page;
    mh->last_page_size = last_page_used;
    list_push_back(&cur->mmap_file_list, &mh->elem);
    if (!mmap_install_page(cur, mh))
    {
      f->eax = MAP_FAILED;
      return;
    }
    f->eax = (uint32_t)mapid;
  }
  else
  {
    f->eax = MAP_FAILED;
    return;
  }
#endif
}

void syscall_munmap(struct intr_frame *f, mapid_t mapid)
{
#ifdef VM
  struct thread *cur = thread_current();
  int i;
  if (list_empty(&cur->mmap_file_list))
  {
    f->eax = MAP_FAILED;
    return;
  }
  struct mmap_handler *mh = syscall_get_mmap_handle(mapid);
  if (mh == NULL)
  {
    f->eax = MAP_FAILED;
    return;
  }
  for (i = 0; i < mh->num_page; i++)
  {
    if (!page_unmap(cur->page_table, mh->mmap_addr + i * PGSIZE))
    {
      delete_mmap_handle(mh);
      f->eax = MAP_FAILED;
      return;
    }
  }
  if (!delete_mmap_handle(mh))
  {
    f->eax = MAP_FAILED;
    return;
  }
#endif
}