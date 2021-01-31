#include <stdlib.h>
#include <stdio.h>
#include <list.h>
#include <block.h>
#include <inode.h>
#include <page.h>
#include "vm/frame.h"

static struct list frame_table;
static struct hash read_only_frames;
static struct lock ft_lock;
static struct list_elem *clock_hand;

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

void init_frame_table(void)
{
  list_init(&frame_table);
  lock_init(&ft_lock);
  hash_init(&read_only_frames, frame_hash, frame_less, NULL);
}

struct frame *frame_alloc(struct page *page)
{
  acquire_fm_lock();
}