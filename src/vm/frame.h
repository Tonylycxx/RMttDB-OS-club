#include <stdbool.h>
#include <list.h>
#include <hash.h>
#include "threads/synch.h"
#include "vm/page.h"

struct frame
{
  void *kpage;
  bool io;
  struct condition io_done;

  struct list_elem ft_elem;
  struct hash_elem ft_hash_elem;
  struct list pages;
  struct lock fm_lock;
};

void init_frame_table(void);

bool evict_frame(void);
struct frame* frame_alloc(struct page *page);
bool evict_frame(void);







