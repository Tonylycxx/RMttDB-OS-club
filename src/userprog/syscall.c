#include <stdio.h>
#include <syscall-nr.h>
#include <console.h>
#include <string.h>
#include "threads/vaddr.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);
static int access_umem(void *, void *, int);
void *is_valid_string(const char *vaddr);

int sys_exec(const void *);
int sys_wait(int pid);
void sys_exit(int exit_code);

/* file syscall */
bool sys_create(const char *, unsigned);
bool sys_remove(const char *file_name);
int sys_open(char *);
int sys_write(int , void *, int);
int sys_read(int , void *, unsigned );
unsigned sys_tell(int );
int sys_filesize(int );
void sys_close(int );
void sys_seek(int fd, unsigned position);
void *is_valid_addr(const void *vaddr);

struct mmap * get_mmap_by_mid(int mid){
  struct thread * cur = thread_current();
  struct list_elem *e;
  for (e=list_begin(&cur->mmap_list);e!=list_end(&cur->mmap_list);e=list_next(e)){
    struct mmap * m = list_entry(e,struct mmap,elem);
    if(m->mid == mid){
      return m;
    }
  }
  return NULL;
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  if(f==NULL){
    sys_exit(-1);
  }
  if(!is_user_vaddr(f->esp)||f->esp==NULL){
    sys_exit(-1);
  }
  is_valid_addr(f->esp);
  
  int syscall_number;
  access_umem(f->esp, &syscall_number, sizeof(syscall_number));

  switch (syscall_number)
  {
    case SYS_HALT:
    {
      shutdown_power_off();
      break;
    }
    case SYS_EXEC:
    {
      char *cmd_line;
      access_umem(f->esp+4, &cmd_line, sizeof(cmd_line));
      is_valid_addr(cmd_line);
      tid_t pid = sys_exec(cmd_line);
      f->eax = pid;
      break;
    }
    case SYS_WAIT:
    {
      tid_t pid;
      access_umem(f->esp+4, &pid, sizeof(pid));
      int ret = sys_wait(pid);
      f->eax = ret;
      break;
    }
    case SYS_EXIT:
    {
      int exit_code;
      access_umem(f->esp+4, &exit_code, sizeof(exit_code));
      sys_exit(exit_code);
      break;
    }
    case SYS_CREATE:
    {
      char *file_name;
      unsigned initial_size;
      access_umem(f->esp+4, &file_name, sizeof(file_name));
      access_umem(f->esp+8, &initial_size, sizeof(initial_size));
      bool flag =  sys_create(file_name, initial_size);
      f->eax = flag;
      break;
    }
    case SYS_REMOVE:
    {
      char *file_name;
      access_umem(f->esp+4, &file_name, sizeof(file_name));
      bool flag = sys_remove(file_name);
      f->eax = flag;
      break;
    }
    case SYS_OPEN:
    {
      char *file_name;
      access_umem(f->esp+4, &file_name, sizeof(file_name));
      int fd = sys_open(file_name);
      f->eax = fd;
      break;
    }
    case SYS_CLOSE:
    {
      int fd;
      access_umem(f->esp+4, &fd, sizeof(fd));
      sys_close(fd);
      break;
    }
    case SYS_WRITE:
    {
      int fd;
      void *buffer;
      unsigned size;

      //fetch args 
      access_umem(f->esp+4, &fd, sizeof(fd));
      access_umem(f->esp+8, &buffer, sizeof(buffer));
      access_umem(f->esp+12, &size, sizeof(size));

      int ret = sys_write(fd, buffer, size);
      f->eax = ret;
      break;

    }
    case SYS_READ:
    {
      int fd;
      void *buffer;
      unsigned size;

      //fetch args 
      access_umem(f->esp+4, &fd, sizeof(fd));
      access_umem(f->esp+8, &buffer, sizeof(buffer));
      access_umem(f->esp+12, &size, sizeof(size));

      int ret = sys_read(fd, buffer, size);
      f->eax = ret;
      break;
    }
    case SYS_FILESIZE:
    {
      int fd;
      access_umem(f->esp+4, &fd, sizeof(fd));
      int size = sys_filesize(fd);
      f->eax = size;
      break;
    }
    case SYS_SEEK:
    {
      int fd;
      unsigned position;
      access_umem(f->esp+4, &fd, sizeof(fd));
      access_umem(f->esp+8, &position, sizeof(position));
      sys_seek(fd, position);
      break;
    }
    case SYS_TELL:
    {
      int fd;
      access_umem(f->esp+4, &fd, sizeof(fd));

      unsigned ret = sys_tell(fd);
      f->eax = ret;
      break;
    }
    case SYS_MMAP:
    {
      int fd;
      void * addr;
      access_umem(f->esp+4,&fd,sizeof(fd));
      access_umem(f->esp+8,&addr,sizeof(void*));
      f->eax= sys_mmap(fd,addr);
      break;
    }
    case SYS_MUNMAP:
    {
      int mid;
      access_umem(f->esp+4,&mid,sizeof(mid));
      sys_unmap(mid);
      break;
    }
    
    default:
      break;
  }
  // thread_exit ();
  return;
}


/*-----------syscall function-------------------*/


int sys_exec(const void *cmd_line)
{
  // enum intr_level old_level = intr_disable();
  if(cmd_line == NULL ){
    sys_exit(-1);
  }
  is_valid_string(cmd_line);
  int pid = process_execute(cmd_line);
  return pid;
  // intr_set_level(old_level);
}

void sys_exit(int exit_code){
  // enum intr_level old_level = intr_disable();
  struct thread *cur = thread_current();
  cur->exit_code = exit_code;
  struct list_elem * e;
  for (e=list_begin(&cur->mmap_list);e!=list_end(&cur->mmap_list);e=list_next(e)){
    struct mmap * m = list_entry(e,struct mmap,elem);
    unmap(m);
  }
  page_exit();
  thread_exit();
  
  // intr_set_level(old_level);
}

int sys_wait(int pid)
{
  int ret = process_wait(pid);
  return ret;
}

bool sys_create(const char *file_name, unsigned initial_size)
{
  if(file_name == NULL || !is_valid_addr(file_name)){
    sys_exit(-1);
  }
  if(strlen(file_name)==0){
    return false;
  }
  lock_acquire(&filesys_lock);
  bool flag = filesys_create(file_name, initial_size);
  lock_release(&filesys_lock);
  return flag;
}

bool sys_remove(const char *file_name)
{
  if(file_name == NULL || !is_valid_addr(file_name)){
    sys_exit(-1);
  }
  if(strlen(file_name)==0){
    return false;
  }
  lock_acquire(&filesys_lock);
  bool flag = filesys_remove(file_name);
  lock_release(&filesys_lock);
  return flag;
}




int sys_open(char *file_name)
{
  
  if(file_name == NULL || !is_valid_addr(file_name)){
    return -1;
  }
  if(strlen(file_name) == 0){
    return -1;
  }

  /* find a empty process_fcb  */
  struct thread *cur = thread_current();
  int i = 0, fd = -1;
  for(i=2; i<MAX_FILES_OPEN; i++){
    if(cur->open_files[i].is_using == 0){
      ASSERT(cur->open_files[i].open_fileptr == NULL);
      lock_acquire(&filesys_lock);
      struct file *file_opened = filesys_open(file_name);
      lock_release(&filesys_lock);
      if(file_opened == NULL){
        return -1;
      }
      cur->open_files[i].is_using = 1;
      cur->open_files[i].open_fileptr = file_opened;
      fd = i;
      break;
    }
  }
  return fd;
}

void sys_close(int fd)
{
  if(fd<0||fd>=MAX_FILES_OPEN){
    sys_exit(-1);
  }
  struct thread *cur = thread_current();
  if(cur->open_files[fd].is_using!=0){
    ASSERT(cur->open_files[fd].open_fileptr!=NULL);
    lock_acquire(&filesys_lock);
    file_close(cur->open_files[fd].open_fileptr);
    lock_release(&filesys_lock);
    cur->open_files[fd].is_using = 0;
    cur->open_files[fd].open_fileptr = NULL;
  }
}


int sys_write(int fd, void *buffer, int size)
{

  if(buffer == NULL || !is_valid_addr(buffer)){
    sys_exit(-1);
  }
  if(fd<0 || fd>MAX_FILES_OPEN){
    sys_exit(-1);
  }
  if(fd == 1){
    putbuf(buffer, size);
    return size;
  }
  else {
    struct thread *cur = thread_current();
    if(cur->open_files[fd].is_using == 0){
      return -1;
    }
    struct file *write_file = cur->open_files[fd].open_fileptr;
    lock_acquire(&filesys_lock);
    int ret = 0;
    while(size>0){
      uint32_t check_page = *(uint32_t*) buffer;
      size_t page_left_bytes = PGSIZE - pg_ofs(buffer);
      size_t page_write_bytes = size<PGSIZE? size:PGSIZE;
      page_write_bytes = page_write_bytes<page_left_bytes?page_write_bytes:page_left_bytes;
      ret += file_write(write_file, buffer, page_write_bytes);
      size -= page_write_bytes;
      buffer += page_write_bytes;
    }
    lock_release(&filesys_lock);
    return ret;
  }
}

int sys_read(int fd, void *buffer, unsigned size)
{
  if(buffer == NULL || buffer < 0x08048000 || !is_user_vaddr(buffer)){
    sys_exit(-1);
  }
  if(fd<0 || fd>MAX_FILES_OPEN){
    sys_exit(-1);
  }
  if(fd == 0){
    input_getc();
    return size;
  }
  else {
    struct thread *cur = thread_current();
    if(cur->open_files[fd].is_using == 0){
      return -1;
    }
    lock_acquire(&filesys_lock);
    struct file *read_file = cur->open_files[fd].open_fileptr;
    int ret =  0;
    while(size>0){
      // printf("hhhh");
      size_t page_left_bytes = PGSIZE - pg_ofs(buffer);
      size_t page_read_bytes = size<PGSIZE?size:PGSIZE;
      page_read_bytes = page_read_bytes<page_left_bytes?page_read_bytes:page_left_bytes;
      ret += file_read(read_file, buffer, page_read_bytes);
      size -= page_read_bytes;
      buffer += page_read_bytes;
    }
    lock_release(&filesys_lock);
    return ret;
  }
}

int sys_filesize(int fd)
{
  struct thread *cur = thread_current();
  if(!(fd >= 0 && fd <= MAX_FILES_OPEN)){
    return -1;
  }
  if(cur->open_files[fd].is_using==0){
    ASSERT(cur->open_files[fd].open_fileptr == NULL);
    return -1;
  }
  struct file *file_temp = cur->open_files[fd].open_fileptr;
  lock_acquire(&filesys_lock);
  int len = file_length(file_temp);
  lock_release(&filesys_lock);
  return len;
}

void sys_seek(int fd, unsigned position){
  struct thread *cur = thread_current();
  if(!(fd >= 0 && fd <= MAX_FILES_OPEN)){
    return ;
  }
  if(cur->open_files[fd].is_using==0){
    ASSERT(cur->open_files[fd].open_fileptr == NULL);
    return ;
  }
  struct file* file_temp = cur->open_files[fd].open_fileptr;
  lock_acquire(&filesys_lock);
  file_seek(file_temp, position);
  lock_release(&filesys_lock);
}

unsigned sys_tell(int fd){
  struct thread *cur = thread_current();
  if(!(fd >= 0 && fd <= MAX_FILES_OPEN)){
    return -1;
  }
  if(cur->open_files[fd].is_using==0){
    ASSERT(cur->open_files[fd].open_fileptr == NULL);
    return -1;
  }
  struct file* file_temp = cur->open_files[fd].open_fileptr;
  lock_acquire(&filesys_lock);
  unsigned ret = file_tell(file_temp);
  lock_release(&filesys_lock);
  return ret;
}

int sys_mmap(int fd,void * addr){
  struct thread * cur = thread_current();
  struct file * f = cur->open_files[fd].open_fileptr;
  struct mmap * m = (struct mmap*)malloc(sizeof(struct mmap));
  uint32_t offset;
  uint32_t length;
  if(m == NULL || addr == NULL || pg_ofs(addr) != 0){
    return -1;
  }
  m->mid = cur->next_mid++;
  lock_acquire(&filesys_lock);
  m->file = file_reopen(f);
  lock_release(&filesys_lock);
  if(m->file == NULL){
    free(m);
    return -1;
  }
  m->addr = addr;
  m->mid = 0;
  list_push_back(&cur->mmap_list,&m->elem);
  offset = 0;
  lock_acquire(&filesys_lock);
  length = file_length(m->file);
  lock_release(&filesys_lock);
  while(length > 0){
    struct page* p = allocate_page((void *)addr+offset,true);
    if(p==NULL){
      unmap(m);
      return -1;
    }
    p->private = false;
    p->page_file = m->file;
    p->file_offset = offset;
    p->file_bytes = length > PGSIZE?PGSIZE:length;
    offset += p->file_bytes;
    length -= p->file_bytes;
    m->page_cnt++;
  }
  return m->mid;
}

void unmap(struct mmap *m){
  list_remove(&m->elem);
  for(int i = 0;i<m->page_cnt;i++){
    if(pagedir_is_dirty(thread_current()->pagedir,((void *)((m->addr)+(i*PGSIZE))))){
      lock_acquire(&filesys_lock);
      file_write_at(m->file,(void*)(m->addr+(i*PGSIZE)),(PGSIZE*(m->page_cnt)),PGSIZE*i);
      lock_release(&filesys_lock);
    }
  }
  for(int i = 0;i<m->page_cnt;i++){
    deallocate_page((void*)(m->addr+i*PGSIZE));
  }
}

void sys_unmap(int mid){
  struct mmap * map = get_mmap_by_mid(mid);
  unmap(map);
}

/* -----memory function -------------*/
int
static
get_user (const uint8_t *uaddr)
{
  int result;
  if (! is_valid_addr(uaddr)) {
    return -1;
  }
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*(uaddr)));
  return result;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static int
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  if (! is_valid_addr(udst)) {
    return -1;
  }
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

/* read "size" bytes of data from src to dst, someting like memcpy */
static int
access_umem (void *src, void *dst, int bytes)
{
  int32_t value;
  int i;
  for(i=0; i<bytes; i++) {
    value = get_user(src + i);
    if(value == -1){
      sys_exit(-1);
    }
    *(char*)((char *)dst + i) = value;
  }
  return bytes;
}

void *
is_valid_addr(const void *vaddr)
{
	struct page *page_ptr = NULL;
	if (vaddr < 0x08048000 || !is_user_vaddr(vaddr) || (page_ptr = page_valid_addr(vaddr))==NULL)
	{
		sys_exit(-1);
		return 0;
	}
	return page_ptr;
}

void *
is_valid_string(const char *vaddr)
{
  char ch = '\0';
  int offset = 0;
  do
  {
    access_umem((char *)(vaddr+offset), &ch, sizeof(ch));
    offset++;
  } while (ch!='\0');
}


