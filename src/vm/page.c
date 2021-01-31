#include <stdlib.h>
#include <stdio.h>
#include <list.h>
#include <block.h>
#include <inode.h>
#include <debug.h>
#include <page.h>
#include <hash.h>
#include "vm/frame.h"

static unsigned frame_hash(const struct hash_elem *e, void *aux UNUSED)
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

static bool frame_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct frame *f_1 = hash_entry(a, struct frame, ft_hash_elem);
  struct frame *f_2 = hash_entry(b, struct frame, ft_hash_elem);
  ASSERT(!list_empty(&f_1->pages));
  ASSERT(!list_empty(&f_2->pages));

  struct page *p_1 = list_entry(list_front(&f_1->pages), struct page, elem);
  struct page *p_2 = list_entry(list_front(&f_2->pages), struct page, elem);
  ASSERT(p_1->page_type & PAGE_TYPE_FILE && p_1->read_only);
  ASSERT(p_2->page_type & PAGE_TYPE_FILE && p_2->read_only);

  block_sector_t sector_1 = inode_get_inumber(file_get_inode(p_1->data.file_info.file));
  block_sector_t sector_2 = inode_get_inumber(file_get_inode(p_2->data.file_info.file));

  if (sector_1 < sector_2)
    return true;
  else if (sector_1 > sector_2)
    return false;
  else
  {
    off_t off_1 = p_1->data.file_info.file_end_offset;
    off_t off_2 = p_2->data.file_info.file_end_offset;

    if (off_1 < off_2)
      return true;
    else
      return false;
  }
}

struct page *create_page_without_param(void)
{
  struct page *new_page = (struct page *)calloc(1, sizeof(struct page));
  return new_page;
}

struct page *create_page_with_param(const void *upage, int type, bool read_only, uint32_t *pd, struct file *f, off_t offset, const void *kpage)
{
  struct page *new_page = (struct page *)calloc(1, sizeof(struct page));
  page_set_upage(new_page, upage);
  page_set_type(new_page, type);
  page_set_readonly(new_page, read_only);
  page_set_pagedir(new_page, pd);
  page_set_fileinfo(new_page, f, offset);
  page_set_kpage(new_page, kpage);
}

void page_set_upage(struct page *p, const void *upage)
{
  p->upage = upage;
}

void page_set_type(struct page *p, int type)
{
  p->page_type = type;
}

void page_set_readonly(struct page *p, bool read_only)
{
  p->read_only = read_only;
}

void page_set_pagedir(struct page *p, uint32_t *pd)
{
  p->pd = pd;
}

void page_set_fileinfo(struct page *p, struct file *file, off_t offset)
{
  p->data.file_info.file = file;
  p->data.file_info.file_end_offset = offset;
}

void page_set_kpage(struct page *p, const void *kpage)
{
  p->data.kpage = kpage;
}