#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <list.h>
#include <hash.h>
#include "threads/synch.h"

struct frame
{
  void *kpage;
  bool io;
  unsigned short fm_lock;

  struct condition io_done;
  struct list_elem ft_elem;
  struct hash_elem ft_hash_elem;
  struct list pages;
};

void init_frame_table(void);
void init_frame(struct frame *f);

bool ft_load_frame(uint32_t *pd, const void *upage, bool write);
void ft_unload_frame(uint32_t *pd, const void *upage);

#endif
