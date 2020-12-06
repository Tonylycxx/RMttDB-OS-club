#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);

void check_valid_addr(const void *ptr_to_check);
void check_valid_argu(char *ptr_to_check);
void getargu(void *esp, int *arg, int argument_number);
struct opened_file *get_op_file(int fd);

#endif /* userprog/syscall.h */
