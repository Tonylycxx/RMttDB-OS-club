#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <hash.h>
#include <list.h>
#include <block.h>
#include <inode.h>
#include <page.h>
#include "vm/frame.h"

#define PAGE_TYPE_ZERO 0x01
#define PAGE_FILE_KERNEL 0x02
#define PAGE_TYPE_FILE 0x04

#define WRITABLE_TO_FILE 0x01
#define WRITABLE_TO_SWAP 0x02

struct file_info
{
  struct file *file;
  off_t file_end_offset;
};

union page_data
{
  const void *kpage;
  block_sector_t swap_sector;
  struct file_info file_info;
};

struct page
{
  void *upage;
  uint32_t *pd;
  uint8_t page_type;
  uint8_t writable;
  bool accessed_or_not;
  bool dirty_or_not;
  bool swapped_or_not;

  struct list_elem elem;
  struct frame *fm;
  struct thread *user;
  union page_data data;
};

static unsigned frame_hash(const struct hash_elem *e, void *aux);
static bool frame_less(const struct hash_elem *a,
                       const struct hash_elem *b,
                       void *aux);

struct page *create_page_with_param(const void *upage, int type, bool readonly, uint32_t *pd, struct file *f, off_t offset, const void *kpage);
struct page *create_page_without_param();
void page_set_upage(struct page *p, const void *upage);
void page_set_type(struct page *p, int type);
void page_set_readonly(struct page *p, bool read_only);
void page_set_pagedir(struct page *p, uint32_t *pd);
void page_set_fileinfo(struct page *p, struct file *file, off_t offset);
void page_set_kpage(struct page *p, const void *kpage);

#endif
