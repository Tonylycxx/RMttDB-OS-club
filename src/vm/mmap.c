#include <stdio.h>
#include "vm/mmap.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/growstack.h"
#include "vm/page.h"

static int
allocate_md(void *upage, struct file *file, size_t num_pages);

static struct file *process_file_get_file(int fd);

/* Maps a file into the user address space starting at virtual address VADDR. */
int mmap(int fd, void *vaddr)
{
  struct thread *cur = thread_current();
  struct file *file = process_file_get_file(fd);
  struct page *page;
  int md;
  off_t length;
  off_t ofs = 0;
  void *upage;
  size_t num_pages;
  size_t i;

  ASSERT(file != NULL);

  if (vaddr == 0 || pg_ofs(vaddr) != 0 || fd == STDIN_FILENO || fd == STDOUT_FILENO || file == NULL)
    return -1;
  length = file_length(file);
  if (length == 0)
    return -1;
  num_pages = ((size_t)pg_round_up((const void *)length)) / PGSIZE;
  /* Do not allow mapping to the space reserved for the stack. */
  for (upage = vaddr, i = 0; i < num_pages; i++, upage += PGSIZE)
    if (pagedir_get_pageinfo(cur->pagedir, upage) != NULL || is_stack_access(upage))
      return -1;
  file = file_reopen(file);
  if (file == NULL)
    return -1;
  md = allocate_md(vaddr, file, num_pages);
  if (md == -1)
  {
    file_close(file);
    return -1;
  }
  /* Map in the file. */
  for (upage = vaddr, i = 0; i < num_pages; i++, upage += PGSIZE)
  {
    page = create_page_without_param();
    if (page == NULL)
    {
      break;
    }
    if (length > PGSIZE)
    {
      ofs += PGSIZE;
      length -= PGSIZE;
    }
    else
      ofs += length;
    page_set_pagedir(page, cur->pagedir);
    page_set_upage(page, upage);
    page_set_type(page, PAGE_TYPE_FILE);
    page_set_fileinfo(page, file, ofs);
    page_set_writable(page, WRITABLE_TO_FILE);
    pagedir_set_pageinfo(cur->pagedir, upage, page);
  }
  if (i < num_pages)
  {
    num_pages = i;
    for (upage = vaddr, i = 0; i < num_pages; i++, upage += PGSIZE)
    {
      /* The frame table will free the page info and clear the entry. */
      ft_unload_frame(cur->pagedir, upage);
    }
    file_close(file);
    return -1;
  }
  return md;
}

void munmap(int md)
{
  struct thread *cur = thread_current();
  struct mmap *mmap;
  void *upage;
  size_t i;

  if (md >= 0 && md < MAX_MMAP_FILES)
  {
    mmap = &cur->mfiles[md];
    if (mmap->file != NULL)
    {
      for (upage = mmap->upage, i = 0; i < mmap->num_pages; i++, upage += PGSIZE)
        ft_unload_frame(cur->pagedir, upage);
      file_close(mmap->file);
      mmap->file = NULL;
    }
  }
}

static int
allocate_md(void *upage, struct file *file, size_t num_pages)
{
  struct thread *cur = thread_current();
  int md = -1;

  /* Allocate the first available map descriptor. */
  for (md = 0; md < MAX_MMAP_FILES; md++)
    if (cur->mfiles[md].file == NULL)
      break;
  if (md < MAX_MMAP_FILES)
  {
    cur->mfiles[md].upage = upage;
    cur->mfiles[md].file = file;
    cur->mfiles[md].num_pages = num_pages;
  }

  return md;
}

static struct file *process_file_get_file(int fd)
{
  struct thread *cur = thread_current();
  struct list_elem *e;

  for(e = list_begin(&cur->opened_files); e != list_end(&cur->opened_files); e = list_next(e))
  {
    struct opened_file * f = list_entry(e, struct opened_file, elem);
    if(f->fd == fd)
      return f->f;
  }
  return NULL;
}