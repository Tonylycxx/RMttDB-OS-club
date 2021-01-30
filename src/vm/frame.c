#include <stdlib.h>
#include <stdio.h>
#include <list.h>
#include <block.h>
#include <inode.h>
#include "vm/frame.h"

static struct list frame_table;
static struct hash read_only_frames;
static struct lock ft_lock;
static unsigned frame_hash(const struct hash_elem *e, void *aux);
static bool frame_less(const struct hash_elem *a,
                       const struct hash_elem *b,
                       void *aux);

static void acquire_fm_lock(void);
static void release_fm_lock(void);

static void acquire_fm_lock(void)
{
  lock_acquire(&ft_lock);
}

static void release_fm_lock(void)
{
  lock_release(&ft_lock);
}

static unsigned frame_hash(const struct hash_elem *e, void *aux)
{
  struct frame *f = hash_entry(e, struct frame, ft_hash_elem);
  struct page *p;
  block_sector_t sector;

  ASSERT(!list_empty(&f->pages));
  p = list_entry(list_front(&f->pages), struct page, elem);
  ASSERT(p->page_type & PAGE_TYPE_FILE && p->read_only);
  sector = inode_get_inumber(file_get_inode(p->data.file_info.file));
  return hash_bytes(&sector, sizeof sector) ^
         hash_bytes(&p->data.file_info.file_end_offset, sizeof p->data.file_info.file_end_offset);
}

void init_frame_table(void)
{
  list_init(&frame_table);
  lock_init(&ft_lock);
}

struct frame *frame_alloc(struct page *page)
{
  acquire_fm_lock();
}