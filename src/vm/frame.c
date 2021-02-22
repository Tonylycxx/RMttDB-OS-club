#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <list.h>
#include <debug.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "filesys/inode.h"
#include "filesys/file.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

static struct hash frame_table;
static struct list frame_clock_list;
static struct lock all_lock;
struct frame_item *current_frame;

void *frame_get_used_frame(void *upage);
void frame_current_clock_to_next();
void frame_current_clock_to_prev();

static bool frame_hash_less(const struct hash_elem *a,
                            const struct hash_elem *b,
                            void *aux UNUSED);
static unsigned frame_hash(const struct hash_elem *e,
                           void *aux UNUSED);

void frame_init()
{
  hash_init(&frame_table, frame_hash, frame_hash_less, NULL);
  list_init(&frame_clock_list);
  lock_init(&all_lock);
  current_frame = NULL;
}

void *frame_get_frame(enum palloc_flags flag, void *upage)
{
  lock_acquire(&all_lock);
  ASSERT(pg_ofs(upage) == 0);
  ASSERT(is_user_vaddr(upage));

  void *frame = palloc_get_page(PAL_USER | flag);

  if (frame == NULL)
  {
    frame = frame_get_used_frame(upage);
    if (flag & PAL_ZERO)
      memset(frame, 0, PGSIZE);
    if (flag & PAL_ASSERT)
      PANIC("frame_get: out of pages");
  }

  if (frame == NULL)
  {
    lock_release(&all_lock);
    return NULL;
  }

  ASSERT(pg_ofs(frame) == 0);
  struct frame_item *tmp = (struct frame_item *)malloc(sizeof(struct frame_item));
  tmp->frame = frame;
  tmp->upage = upage;
  tmp->t = thread_current();
  tmp->pinned = true;
  hash_insert(&frame_table, &tmp->hash_elem);
  lock_release(&all_lock);
  return frame;
}

void frame_free_frame(void *frame)
{
  lock_acquire(&all_lock);
  struct frame_item *t = frame_lookup(frame);
  if (t == NULL)
    PANIC("try_free_a frame_that_not_exist!!");
  if (!t->pinned)
  {
    if (current_frame == t)
    {
      if (list_empty(&frame_clock_list))
        current_frame = NULL;
      else
        frame_current_clock_to_next();
    }
    list_remove(&t->list_elem);
  }
  hash_delete(&frame_table, &t->hash_elem);
  free(t);
  palloc_free_page(frame);
  lock_release(&all_lock);
}

bool frame_get_pinned(void *frame)
{
  struct frame_item *t = frame_lookup(frame);
  if (t == NULL)
    PANIC("try_set_pinned_of_a frame_that_not_exist!!");
  return t->pinned;
}

bool frame_set_pinned_false(void *frame)
{
  lock_acquire(&all_lock);

  struct frame_item *t = frame_lookup(frame);
  if (t == NULL)
  {
    lock_release(&all_lock);
    return false;
  }

  if (t->pinned == false)
  {
    lock_release(&all_lock);
    return true;
  }

  t->pinned = false;
  list_push_back(&frame_clock_list, &t->list_elem);
  if (list_size(&frame_clock_list) == 1)
    current_frame = t;
  lock_release(&all_lock);
  return true;
}

void *frame_get_used_frame(void *upage)
{
  ASSERT(current_frame != NULL);
  while (pagedir_is_accessed(current_frame->t->pagedir, current_frame->upage))
  {
    pagedir_set_accessed(current_frame->t->pagedir, current_frame->upage, false);
    frame_current_clock_to_next();
    ASSERT(current_frame != NULL);
  }
  struct frame_item *t = current_frame;
  void *tmp_frame = t->frame;
  index_t index = (index_t)-1;
  ASSERT(page_find(current_frame->t->page_table, current_frame->upage) != NULL);
  struct page_table_elem *e = page_find(current_frame->t->page_table, current_frame->upage);
  if (e == NULL || e->origin == NULL || ((struct mmap_handler *)(e->origin))->is_static_data)
  {
    index = swap_store(current_frame->frame);
    if (index == -1)
      return NULL;
    ASSERT(page_status_eviction(current_frame->t, current_frame->upage, index, true));
  }
  else
  {
    mmap_write_file(e->origin, current_frame->upage, tmp_frame);
    ASSERT(page_status_eviction(current_frame->t, current_frame->upage, index, false));
  }

  list_remove(&t->list_elem);
  if (list_empty(&frame_clock_list))
    current_frame = NULL;
  else
    frame_current_clock_to_next();
  hash_delete(&frame_table, &t->hash_elem);
  free(t);
  return tmp_frame;
}

void frame_current_clock_to_next()
{
  ASSERT(current_frame != NULL);
  if (list_size(&frame_clock_list) == 1)
    return;
  if (&current_frame->list_elem == list_back(&frame_clock_list))
    current_frame = list_entry(list_head(&frame_clock_list),
                               struct frame_item, list_elem);

  current_frame = list_entry(list_next(&current_frame->list_elem),
                             struct frame_item, list_elem);
}

void frame_current_clock_to_prev()
{
  ASSERT(current_frame != NULL);
  if (list_size(&frame_clock_list) == 1)
    return;

  if (&current_frame->list_elem == list_front(&frame_clock_list))
    current_frame = list_entry(list_tail(&frame_clock_list),
                               struct frame_item, list_elem);

  current_frame = list_entry(list_prev(&current_frame->list_elem),
                             struct frame_item, list_elem);
}

void *frame_lookup(void *frame)
{
  struct frame_item p;
  struct hash_elem *e;
  p.frame = frame;
  e = hash_find(&frame_table, &p.hash_elem);
  return e == NULL ? NULL : hash_entry(e, struct frame_item, hash_elem);
}

static bool frame_hash_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  const struct frame_item *ta = hash_entry(a, struct frame_item, hash_elem);
  const struct frame_item *tb = hash_entry(b, struct frame_item, hash_elem);
  return ta->frame < tb->frame;
}

static unsigned frame_hash(const struct hash_elem *e, void *aux UNUSED)
{
  struct frame_item *t = hash_entry(e, struct frame_item, hash_elem);
  return hash_bytes(&t->frame, sizeof(t->frame));
}
