#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "vm/page.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
void argument_stack(char **parse, int count, void **esp);

/*for file manipulation*/
int process_add_file (struct file *f);
struct file* process_get_file (int fd);
void process_close_file (int fd);

/*for child process*/
struct thread *get_child_process (int pid);
void remove_child_process(struct thread *cp);

bool handle_mm_fault(struct vm_entry *vme);

#endif /* userprog/process.h */
