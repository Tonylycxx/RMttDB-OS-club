#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <list.h>
#include <debug.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "filesys/inode.h"
#include "filesys/file.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

static struct list frame_table;
static struct hash read_only_frames;
static struct lock ft_lock;
static struct list_elem *clock_hand;

static void acquire_fm_lock(void);
static void release_fm_lock(void);
static struct frame *frame_alloc(void);
static struct frame *evict_frame(void);
static void *get_frame_to_evict(void);
static bool load_frame(uint32_t *pd, const void *upage, bool write, bool keep_locked);
static void map_page(struct page *page, struct frame *frame, const void *upage);
static struct frame *lookup_read_only_frame(struct page *page);
static void wait_for_io_done(struct frame **frame);

static inline off_t get_offset(off_t file_end_offset);
static inline off_t get_size(off_t file_end_offset);

static inline off_t get_offset(off_t file_end_offset)
{
  return file_end_offset > 0 ? (file_end_offset - 1) & ~PGMASK : 0;
}

static inline off_t get_size(off_t file_end_offset)
{
  return file_end_offset - get_offset(file_end_offset);
}

static void acquire_fm_lock(void)
{
  lock_acquire(&ft_lock);
}

static void release_fm_lock(void)
{
  lock_release(&ft_lock);
}

void init_frame_table(void)
{
  list_init(&frame_table);
  lock_init(&ft_lock);
  hash_init(&read_only_frames, frame_hash, frame_less, NULL);
}

void init_frame(struct frame *f)
{
  list_init(&f->pages);
  cond_init(&f->io_done);
}

static struct frame *frame_alloc(void)
{
  struct frame *new_frame;
  void *kpage;

  kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  if (kpage != NULL)
  {
    new_frame = (struct frame *)calloc(1, sizeof(struct frame));
    if (new_frame != NULL)
    {
      init_frame(new_frame);
      new_frame->kpage = kpage;
      if (!list_empty(&frame_table))
        list_insert(clock_hand, &new_frame->ft_elem);
      else
      {
        list_push_front(&frame_table, &new_frame->ft_elem);
        clock_hand = list_begin(&frame_table);
      }
    }
    else
      palloc_free_page(kpage);
  }
  else
    new_frame = evict_frame();
  return new_frame;
}

static void *get_frame_to_evict(void)
{
  struct frame *frame;
  struct frame *start;
  struct frame *found = NULL;
  struct page *page;
  struct list_elem *e;
  bool accessed;

  ASSERT(!list_empty(&frame_table));
  start = list_entry(clock_hand, struct frame, ft_elem);
  frame = start;
  do
  {
    if (frame->fm_lock == 0)
    {
      accessed = false;
      ASSERT(!list_empty(&frame->pages));
      for (e = list_begin(&frame->pages); e != list_end(&frame->pages); e = list_next(e))
      {
        page = list_entry(e, struct page, elem);
        accessed = accessed || pagedir_is_accessed(page->pd, page->upage);
        pagedir_set_accessed(page->pd, page->upage, false);
      }
      if (!accessed)
        found = frame;
    }
    clock_hand = list_next(clock_hand);
    if (clock_hand == list_end(&frame_table))
      clock_hand = list_begin(&frame_table);
    frame = list_entry(clock_hand, struct frame, ft_elem);
  } while (!found && frame != start);
  if (found == NULL)
  {
    ASSERT(frame == start);
    if (frame->fm_lock > 0)
      PANIC("No available frame to evict");
    found = frame;
    clock_hand = list_next(clock_hand);
    if (clock_hand == list_end(&frame_table))
      clock_hand = list_begin(&frame_table);
  }
  return found;
}

static struct frame *evict_frame(void)
{
  struct frame *frame;
  struct page *page;
  struct file_info *file_info;
  off_t bytes_written;
  block_sector_t swap_sector;
  struct list_elem *e;
  bool dirty = false;

  frame = get_frame_to_evict();
  for (e = list_begin(&frame->pages); e != list_end(&frame->pages); e = list_next(e))
  {
    page = list_entry(e, struct page, elem);
    dirty = dirty || pagedir_is_dirty(page->pd, page->upage);
    pagedir_clear_page(page->pd, page->upage);
  }

  if (dirty || page->writable & WRITABLE_TO_SWAP)
  {
    ASSERT(page->writable != 0);
    frame->io = true;
    frame->fm_lock++;
    if (page->writable & WRITABLE_TO_FILE)
    {
      file_info = &page->data.file_info;
      lock_release(&ft_lock);
      bytes_written = file_write_at(file_info->file, frame->kpage,
                                    get_size(file_info->file_end_offset),
                                    get_offset(file_info->file_end_offset));
      ASSERT(bytes_written == get_size(file_info->file_end_offset));
    }
    else
    {
      lock_release(&ft_lock);
      swap_sector = swap_write(frame->kpage);
    }
    lock_acquire(&ft_lock);
    frame->fm_lock--;
    frame->io = false;
    cond_broadcast(&frame->io_done, &ft_lock);
  }
  else if (page->page_type & PAGE_TYPE_FILE && page->writable == 0)
  {
    ASSERT(hash_find(&read_only_frames, &frame->ft_hash_elem) != NULL);
    hash_delete(&read_only_frames, &frame->ft_hash_elem);
  }
  for (e = list_begin(&frame->pages); e != list_end(&frame->pages);)
  {
    page = list_entry(list_front(&frame->pages), struct page, elem);
    page->fm = NULL;
    if (page->writable & WRITABLE_TO_SWAP)
    {
      page->swapped_or_not = true;
      page->data.swap_sector = swap_sector;
    }
    e = list_remove(e);
  }
  memset(frame->kpage, 0, PGSIZE);
  return frame;
}

static bool load_frame(uint32_t *pd, const void *upage, bool write, bool keep_locked)
{
  struct page *page;
  struct file_info *file_info;
  struct frame *frame = NULL;
  void *kpage;
  off_t bytes_read;
  bool success = false;

  ASSERT(is_user_vaddr(upage));
  page = pagedir_get_pageinfo(pd, upage);
  if (page == NULL || (write & page->writable == 0))
    return success;
  lock_acquire(&ft_lock);
  wait_for_io_done(&page->fm);
  ASSERT(page->fm == NULL || keep_locked);
  if (page->fm != NULL)
  {
    page->fm->fm_lock++;
    lock_release(&ft_lock);
    return true;
  }

  if (page->page_type & PAGE_TYPE_FILE && page->writable == 0)
  {
    frame = lookup_read_only_frame(page);
    if (frame != NULL)
    {
      map_page(page, frame, upage);
      frame->fm_lock++;
      wait_for_io_done(&frame);
      frame->fm_lock--;
      success = true;
    }
  }

  if (frame == NULL)
  {
    frame = frame_alloc();
    if (frame != NULL)
    {
      map_page(page, frame, upage);
      if (page->swapped_or_not || page->page_type & PAGE_TYPE_FILE)
      {
        frame->io = true;
        frame->fm_lock++;
        if (page->swapped_or_not)
        {
          lock_release(&ft_lock);
          swap_read(page->data.swap_sector, frame->kpage);
          page->swapped_or_not = false;
        }
        else
        {
          if (page->writable == 0)
          {
            hash_insert(&read_only_frames, &frame->ft_hash_elem);
          }
          file_info = &page->data.file_info;
          lock_release(&ft_lock);
          bytes_read = file_read_at(file_info->file, frame->kpage,
                                    get_size(file_info->file_end_offset),
                                    get_offset(file_info->file_end_offset));
          ASSERT(bytes_read == get_size(file_info->file_end_offset));
        }
        lock_acquire(&ft_lock);
        frame->fm_lock--;
        frame->io = false;
        cond_broadcast(&frame->io_done, &ft_lock);
      }
      else if (page->page_type & PAGE_TYPE_KERNEL)
      {
        kpage = (void *)page->data.kpage;
        ASSERT(kpage != NULL);
        memcpy(frame->kpage, kpage, PGSIZE);
        palloc_free_page(kpage);
        page->data.kpage = NULL;
        page->page_type = PAGE_TYPE_ZERO;
      }
      success = true;
    }
  }
  if (success && keep_locked)
    frame->fm_lock++;
  lock_release(&ft_lock);
  return success;
}

static void wait_for_io_done(struct frame **frame)
{
  while (*frame != NULL && (*frame)->io)
    cond_wait(&(*frame)->io_done, &ft_lock);
}

static void map_page(struct page *page, struct frame *frame, const void *upage)
{
  page->fm = frame;
  list_push_back(&frame->pages, &page->elem);
  pagedir_set_page(page->pd, upage, frame->kpage, page->writable != 0);
  pagedir_set_dirty(page->pd, upage, false);
  pagedir_set_accessed(page->pd, upage, true);
}

static struct frame *lookup_read_only_frame(struct page *page)
{
  struct frame frame;
  struct hash_elem *e;

  list_init(&frame.pages);
  list_push_back(&frame.pages, &page->elem);
  e = hash_find(&read_only_frames, &frame.ft_hash_elem);
  return e != NULL ? hash_entry(e, struct frame, ft_hash_elem) : NULL;
}

bool ft_load_frame(uint32_t *pd, const void *upage, bool write)
{
  return load_frame(pd, upage, write, false);
}

void ft_unload_frame(uint32_t *pd, const void *upage)
{
  struct page *page, *p;
  struct file_info *file_info;
  void *kpage;
  struct frame *frame;
  off_t bytes_written;
  struct list_elem *e;

  ASSERT(is_user_vaddr(upage));
  page = pagedir_get_pageinfo(pd, upage);
  if (page == NULL)
    return;
  lock_acquire(&ft_lock);
  wait_for_io_done(&page->fm);
  if (page->fm != NULL)
  {
    frame = page->fm;
    page->fm = NULL;
    if (list_size(&frame->pages) > 1)
    {
      for (e = list_begin(&frame->pages); e != list_end(&frame->pages); e = list_next(e))
      {
        p = list_entry(e, struct page, elem);
        if (page == p)
        {
          list_remove(e);
          break;
        }
      }
    }
    else
    {
      ASSERT(list_entry(list_begin(&frame->pages), struct page, elem) == page);
      if (page->page_type & PAGE_TYPE_FILE && page->writable == 0)
        hash_delete(&read_only_frames, &frame->ft_hash_elem);
      if (clock_hand == &frame->ft_elem)
      {
        clock_hand = list_next(clock_hand);
        if (clock_hand == list_end(&frame_table))
          clock_hand = list_begin(&frame_table);
      }
      list_remove(&page->elem);
      list_remove(&frame->ft_elem);
    }
    pagedir_clear_page(page->pd, upage);
    lock_release(&ft_lock);
    if (list_empty(&frame->pages))
    {
      if (page->writable & WRITABLE_TO_FILE && pagedir_is_dirty(page->pd, upage))
      {
        ASSERT(page->writable != 0);
        file_info = &page->data.file_info;
        bytes_written = file_write_at(file_info->file, frame->kpage,
                                      get_size(file_info->file_end_offset),
                                      get_offset(file_info->file_end_offset));
        ASSERT(bytes_written == get_size(file_info->file_end_offset));
      }
      palloc_free_page(frame->kpage);
      ASSERT(frame->fm_lock = 0);
      free(frame);
    }
  }
  else
  {
    lock_release(&ft_lock);
  }
  if (page->swapped_or_not)
  {
    swap_release(page->data.swap_sector);
    page->swapped_or_not = false;
  }
  else if (page->page_type & PAGE_TYPE_KERNEL)
  {
    kpage = (void *)page->data.kpage;
    ASSERT(kpage != NULL);
    palloc_free_page(kpage);
    page->data.kpage = NULL;
  }
  pagedir_set_pageinfo(page->pd, upage, NULL);
  free(page);
}

bool ft_lock_frame(uint32_t *pd, const void *upage, bool write)
{
  return load_frame(pd, upage, write, true);
}

void ft_unlock_frame(uint32_t *pd, const void *upage)
{
  struct page *page;
  ASSERT(is_user_vaddr);
  page = pagedir_get_pageinfo(pd, upage);
  if (page == NULL)
    return;
  ASSERT(page->fm != NULL);
  lock_acquire(&ft_lock);
  page->fm->fm_lock--;
  lock_release(&ft_lock);
}