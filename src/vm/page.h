#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <hash.h>
#include <list.h>
#include "threads/palloc.h"

typedef struct hash page_table_t;

enum page_status
{
  FRAME,
  SWAP,
  FILE
};

struct page_table_elem
{
  void *key, *value, *origin;
  enum page_status status;
  bool writable;
  struct hash_elem elem;
};

void page_lock_init();
page_table_t *page_create();
bool page_init(page_table_t *page_table);
void page_destroy(page_table_t *page_table);
struct page_table_elem *page_find(page_table_t *page_table, void *upage);
struct page_table_elem *page_find_with_lock(page_table_t *page_table, void *upage);

bool page_pagefault_handler(const void *vaddr, bool to_write, void *esp);

bool page_set_frame(void *upage, void *kpage, bool writable);
bool page_available_upage(page_table_t *page_table, void *upage);
bool page_install_file(page_table_t *page_table, struct mmap_handler *mh, void *key);
bool page_status_eviction(struct thread *cur, void *upage, void *index, bool to_swap);
bool page_unmap(page_table_t *page_table, void *upage);

#endif
