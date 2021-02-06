#include <stdlib.h>
#include <stdio.h>
#include <list.h>
#include <debug.h>
#include <hash.h>
#include "devices/block.h"
#include "filesys/inode.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

#define PAGE_PAL_FLAG 0
#define PAGE_INST_MARGIN 32
#define PAGE_STACK_SIZE 0x800000
#define PAGE_STACK_UNDERLINE (PHYS_BASE - PAGE_STACK_SIZE)

static bool page_hash_less(const struct hash_elem *lhs, const struct hash_elem *rhs, void *aux UNUSED);
static unsigned page_hash(const struct hash_elem *e, void *aux UNUSED);

static void page_destroy_frame_likes(struct hash_elem *e, void *aux UNUSED);

static struct lock page_lock;

void page_lock_init()
{
  lock_init(&page_lock);
}

page_table_t *page_create()
{
  page_table_t *pt = (page_table_t *)malloc(sizeof(page_table_t));

  if (pt != NULL)
  {
    if (page_init(pt) == false)
    {
      free(pt);
      return NULL;
    }
    else
    {
      return pt;
    }
  }
  else
  {
    return NULL;
  }
}

bool page_init(page_table_t *page_table)
{
  return hash_init(page_table, page_hash, page_hash_less, NULL);
}

void page_destroy(page_table_t *page_table)
{
  lock_acquire(&page_lock);
  hash_destroy(page_table, page_destroy_frame_likes);
  lock_release(&page_lock);
}

struct page_table_elem *page_find(page_table_t *page_table, void *upage)
{
  struct hash_elem *e;
  struct page_table_elem tmp;

  ASSERT(page_table != NULL);
  tmp.key = upage;
  e = hash_find(page_table, &(tmp.elem));

  if (e != NULL)
    return hash_entry(e, struct page_table_elem, elem);
  else
    return NULL;
}

bool page_pagefault_handler(const void *vaddr, bool to_write, void *esp)
{
  struct thread *cur = thread_current();
  page_table_t *page_table = cur->page_table;
  uint32_t *pagedir = cur->pagedir;
  void *upage = pg_round_down(vaddr);

  bool success = true;
  lock_acquire(&page_lock);
  struct page_table_elem *t = page_find(page_table, upage);
  void *dst = NULL;

  ASSERT(is_user_vaddr(vaddr));
  ASSERT(!(t != NULL && t->status == FRAME));

  if (to_write == true && t != NULL && t->writable == false)
    return false;

  if (upage >= PAGE_STACK_UNDERLINE)
  {
    if (vaddr >= esp - PAGE_INST_MARGIN)
    {
      if (t == NULL)
      {
        dst = frame_get_frame(PAGE_PAL_FLAG, upage);
        if (dst == NULL)
          success = false;
        else
        {
          t = (struct page_table_elem *)malloc(sizeof(struct page_table_elem));
          t->key = upage;
          t->value = dst;
          t->status = FRAME;
          t->writable = true;
          t->origin = NULL;
          hash_insert(page_table, &t->elem);
        }
      }
      else
      {
        switch (t->status)
        {
        case SWAP:
          dst = frame_get_frame(PAGE_PAL_FLAG, upage);
          if (dst == NULL)
          {
            success = false;
            break;
          }
          swap_load((index_t)t->value, dst);
          t->value = dst;
          t->status = FRAME;
          break;

        default:
          success = false;
        }
      }
    }
    else
      success = false;
  }
  else
  {
    if (t == NULL)
      success = false;
    else
    {
      switch (t->status)
      {
      case SWAP:
        dst = frame_get_frame(PAGE_PAL_FLAG, upage);
        if (dst == NULL)
        {
          success = false;
          break;
        }
        swap_load((index_t)t->value, dst);
        t->value = dst;
        t->status = FRAME;
        break;

      case FILE:
        dst = frame_get_frame(PAGE_PAL_FLAG, upage);
        if (dst == NULL)
        {
          success = false;
          break;
        }
        mmap_read_file(t->value, upage, dst);
        t->value = dst;
        t->status = FRAME;
        break;

      default:
        success = false;
      }
    }
  }

  frame_set_pinned_false(dst);
  lock_release(&page_lock);
  if (success)
    ASSERT(pagedir_set_page(pagedir, t->key, t->value, t->writable));
  return success;
}

bool page_set_frame(void *upage, void *kapge, bool writable)
{
  struct thread *cur = thread_current();
  page_table_t *page_table = cur->page_table;
  uint32_t *pagedir = cur->pagedir;

  bool success = true;
  lock_acquire(&page_lock);

  struct page_table_elem *t = page_find(page_table, upage);
  if (t == NULL)
  {
    t = (struct page_table_elem *)malloc(sizeof(struct page_table_elem));
    t->key = upage;
    t->value = kapge;
    t->status = FRAME;
    t->origin = NULL;
    t->writable = writable;
    hash_insert(page_table, &t->elem);
  }
  else
    success = false;
  lock_release(&page_lock);
  if (success)
    ASSERT(pagedir_set_page(pagedir, t->key, t->value, t->writable));
  return success;
}

bool page_available_upage(page_table_t *page_table, void *upage)
{
  return upage < PAGE_STACK_UNDERLINE && page_find(page_table, upage) == NULL;
}

bool page_accessible_upage(page_table_t *page_table, void *upage)
{
  return upage < PAGE_STACK_UNDERLINE && page_find(page_table, upage) != NULL;
}

bool page_install_file(page_table_t *page_table, struct mmap_handler *mh, void *key)
{
  struct thread *cur = thread_current();
  bool success = true;
  lock_acquire(&page_lock);
  if (page_available_upage(page_table, key))
  {
    struct page_table_elem *e = (struct page_table_elem *)malloc(sizeof(struct page_table_elem));
    e->key = key;
    e->value = mh;
    e->status = FILE;
    e->writable = mh->writable;
    e->origin = mh;
    hash_insert(page_table, &e->elem);
  }
  else
    success = false;
  lock_release(&page_lock);
  return success;
}

bool page_unmap(page_table_t *page_table, void *upage)
{
  struct thread *cur = thread_current();
  bool success = true;
  lock_acquire(&page_lock);

  if (page_accessible_upage(page_table, upage))
  {
    struct page_table_elem *t = page_find(page_table, upage);

    ASSERT(t != NULL);
    switch (t->status)
    {
    case FILE:
      hash_delete(page_table, &t->elem);
      free(t);
      break;

    case FRAME:
      if (pagedir_is_dirty(cur->pagedir, t->key))
        mmap_write_file(t->origin, t->key, t->value);
      pagedir_clear_page(cur->pagedir, t->key);
      hash_delete(page_table, &t->elem);
      frame_free_frame(t->value);
      free(t);
      break;

    default:
      success = false;
    }
  }
  else
    success = false;
  lock_release(&page_lock);
  return success;
}

bool page_status_eviction(struct thread *cur, void *upage, void *index, bool to_swap)
{
  struct page_table_elem *t = page_find(cur->page_table, upage);
  bool success = true;

  if (t != NULL && t->status == FRAME)
  {
    if (to_swap)
    {
      t->value = index;
      t->status = SWAP;
    }
    else
    {
      ASSERT(t->origin != NULL);
      t->value = t->origin;
      t->status = FILE;
    }
    pagedir_clear_page(cur->pagedir, upage);
  }
  else {
    if(t != NULL)
      printf("%s\n", t->status == FILE ? "file" : "swap");
    else
      puts("NULL");
    success = false;
  }
  return success;
}

bool page_hash_less(const struct hash_elem *lhs, const struct hash_elem *rhs, void *aux UNUSED)
{
  return hash_entry(lhs, struct page_table_elem, elem)->key < hash_entry(rhs, struct page_table_elem, elem)->key;
}

unsigned page_hash(const struct hash_elem *e, void *aux UNUSED)
{
  struct page_table_elem *t = hash_entry(e, struct page_table_elem, elem);
  return hash_bytes(&t->key, sizeof(t->key));
}

void page_destroy_frame_likes(struct hash_elem *e, void *aux UNUSED)
{
  struct page_table_elem *t = hash_entry(e, struct page_table_elem, elem);

  if(t->status == FRAME)
  {
    pagedir_clear_page(thread_current()->pagedir, t->key);
    frame_free_frame(t->value);
  }
  else if(t->status == SWAP)
    swap_free((index_t) t->value);
  free(t);
}

struct page_table_elem *page_find_with_lock(page_table_t *page_table, void *upage)
{
  lock_acquire(&page_lock);
  struct page_table_elem *tmp = page_find(page_table, upage);
  lock_release(&page_lock);
  return tmp;
}