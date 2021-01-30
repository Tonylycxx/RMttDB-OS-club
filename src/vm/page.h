#include <stdbool.h>
#include <hash.h>

struct file_info
{
  struct file *file;
  off_t file_end_offset;
};

union page_data
{
  const void* kpage;
  block_sector_t swap_sector;
  struct file_info file_info;
};


struct page
{
  void *upage;
  uint32_t *pd;
  uint8_t page_type;
  bool read_only;
  bool accessed_or_not;
  bool dirty_or_not;
  bool swapped_or_not;

  struct list_elem elem;
  struct frame* fm;
  struct thread* user;
  union page_data data;
};
