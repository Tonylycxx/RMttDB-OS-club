#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);

// void check_valid_addr(const void *ptr_to_check);
// void check_valid_argu(char *ptr_to_check);
// void getargu(void *esp, int *arg, int argument_number);

bool syscall_check_user_vaddr(const void *vaddr, bool write);
bool syscall_check_user_string(const char *ustr);
bool syscall_check_user_buffer(const char *ustr, int size, bool write);
void thread_exit_with_retval(struct intr_frame *f, int ret_val);
struct opened_file *get_op_file(int fd);

#endif /* userprog/syscall.h */
