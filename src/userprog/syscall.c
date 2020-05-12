#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/vaddr.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "filesys/off_t.h"



static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock); 
}

void check_address(void *addr){
	if(!(is_user_vaddr(addr) && addr>=((void *) 0x8048000))){
		exit(-1);
	}
	/*if(pagedir_get_page(thread_current()->pagedir, addr) == NULL){
		exit(-1);
	}*/
}

void get_argument(void *esp, int *arg , int count) { 
	int i;
/* 유저 스택에 저장된 인자값들을 커널로 저장 */ 
	for(i=0; i<count; i++){
	/* 인자가 저장된 위치가 유저영역인지 확인 */ 
		check_address(esp+4*(i+1));
		check_address(esp+4*(i+1)+3);
		arg[i] = *(int*)(esp+4*(i+1));
	}
} 



static void
syscall_handler (struct intr_frame *f) 
{
 	int arg[5];
 	uint32_t *sp = f -> esp; /* userstack pointer */ 
 	check_address((void *)sp); 
 	int syscall_n = *sp;   /* system call number */
 	//int check = 0;

 	switch(syscall_n){
		case SYS_HALT:
			halt();
			break;                   
    	case SYS_EXIT:
    		get_argument(sp,arg,1);
    		exit(arg[0]);
			break;                     
    	case SYS_EXEC:
    		get_argument(sp , arg , 1); 
    		check_address((void *)arg[0]); 
    		f -> eax = exec((const char *)arg[0]); 
			break;                     
    	case SYS_WAIT:
    		get_argument(sp,arg,1);
		  	f->eax = wait((tid_t)arg[0]);
			break;                 
    	case SYS_CREATE: 
    		get_argument(sp,arg,2);
    		check_address((void*)arg[0]);
    		check_address((void*)arg[0]+strlen((char*)arg[0]));
    		f -> eax = create((const char*)arg[0],(unsigned)arg[1]);
			break;                   
    	case SYS_REMOVE:
    		get_argument(sp,arg,1);
    		check_address((void*)arg[0]);
    		check_address((void*)arg[0]+strlen((char*)arg[0]));
    		f -> eax = remove((const char*)arg[0]);
			break;                     
    	case SYS_OPEN: 
    		get_argument(sp,arg,1);
    		check_address((void*)arg[0]);
    		check_address((void*)arg[0]+strlen((char*)arg[0]));
    		f -> eax = open((const char*)arg[0]);
			break;                      
    	case SYS_FILESIZE:  
    		get_argument(sp,arg,1);
    		f -> eax = filesize(arg[0]); 
			break;               
    	case SYS_READ:   
    		get_argument(sp,arg,3);
    		check_address((void*)arg[1]);
    		check_address((void*)arg[1]+(unsigned)arg[2]);
    		/*for(check=0;check<(signed)arg[2];check++){
    			if(!get_user((void*)arg[1]+check))
    				exit(-1);
    		}*/
    		f -> eax = read(arg[0],(void*)arg[1], (unsigned)arg[2]);  
			break;                 
    	case SYS_WRITE: 
    		get_argument(sp,arg,3);
    		check_address((void*)arg[1]);
    		check_address((void*)arg[1]+(unsigned)arg[2]);
    		/*for(check=0;check<(signed)arg[2];check++){
    			if(!put_user((void*)arg[1]+check,0))
    				exit(-1);
    		}*/
    		f -> eax = write(arg[0],(void*)arg[1], (unsigned)arg[2]);  
			break;                 
    	case SYS_SEEK:  
    		get_argument(sp,arg,2);
    		seek(arg[0],(unsigned)arg[1]);  
			break;                  
    	case SYS_TELL:   
    		get_argument(sp,arg,1);
    		f -> eax = tell(arg[0]);  
			break;                 
    	case SYS_CLOSE:  
    		get_argument(sp,arg,1);
    		close(arg[0]);
			break;  
		/*default:
			exit(-1);*/
 	}
}

void halt (void) { 
	/* quit pintos using shutdown_power_off()*/ 
	shutdown_power_off();
} 

void exit (int status) { 
	/* bring current running process*/
	struct thread *t = thread_current(); 
	/*save exit status to process descriptor*/
	t->exit_status = status;
	/*print the process exit message as "process name : exit(status)"*/
	printf("%s: exit(%d)\n",t->name,status);    
	/* exit thread */ 
	thread_exit();
} 

tid_t exec (const char *cmd_line){
	/* create child process by process_execute() */
	tid_t child;
	child = process_execute (cmd_line);
	/* search pid of child process */
	struct thread *t;
	t = get_child_process (child);
	/* wait until child process load */
	sema_down(&(t->sema_load));
	/* return result */
	if (t->load)
		return child;
	else
		return -1;
}

int wait (tid_t tid){
	return process_wait(tid);
} 
bool create(const char *file , unsigned initial_size) { 
	/* create file with name : file and size : initial_size */ 
	/* return true when success, false when it fails */ 
	if(filesys_create(file, initial_size))
		return true;
	return false;
} 

bool remove(const char *file) { 
	/* remove the file with name : file */ 
	/* success return true, fail return false */
	if(filesys_remove(file))
		return true;
	return false;
} 
int open(const char *file) { 
	/* open file*/ 
	struct file *open_file = filesys_open(file); 
	/* return -1 if open_file does not exist */ 
	if(!open_file)
		return -1;
	/*give fd to open_file and return fd */
	return process_add_file(open_file); 
}
int filesize (int fd) { 
	int length;
	/* search file using file director*/ 
	struct file *searching_file = process_get_file(fd);
	/* return -1 if searching_file does not exist */ 
	if(!searching_file)
		return -1;

	/* return file length */ 
	length = file_length(searching_file); 
	return length;
} 
int read (int fd, void *buffer, unsigned size) { 
	int size_read=0;
	char* char_buffer = (char*)buffer;
	/* use lock to prevent simultaneous approach*/ 
	lock_acquire(&filesys_lock);
	/* if fd is 0, save keyboard input to buffer and return saved size(use input_getc()) */ 
	if(fd==0){
		while((unsigned)size_read < size){
			*(char_buffer+size_read)=input_getc();
			if(*(char_buffer+size_read)=='\n')
				break;
			size_read++;
		}
		*(char_buffer+size_read)='\0';
	}
	/* if fd is not 0, save the file through the data size and return the read byte number*/ 
	else{	
		/* search file using file director*/ 
		struct file *searching_file = process_get_file(fd);
		if(searching_file==NULL){
			lock_release(&filesys_lock);
			return -1;
		}
		size_read = file_read(searching_file,buffer,size);
	}
	lock_release(&filesys_lock);
	return size_read;
} 
int write(int fd, void *buffer, unsigned size) { 
	int size_write=0;
	//int check;
	/* use lock to prevent simultaneous approach*/ 
	lock_acquire(&filesys_lock);
	/* If fd is 1, print the save value in buffer and return the buffer size(use putbuf()) */ 
	if(fd==1){
		putbuf((const char *)buffer,size);
		size_write = size;
	}
	/* If fd is not 1 write the data to the file for the size and return the written byte number */ 
	else{	
		/* search file using file director*/ 
		struct file *searching_file = process_get_file(fd);
		if(searching_file==NULL){
			lock_release(&filesys_lock);
			return 0;
		}
		/*else if(!put_user((uint8_t*)buffer,size)){
			lock_release(&filesys_lock);
    		exit(-1);
		}*/
		/*for(check=0;check<(signed)size;check++){
    		if(!put_user(buffer,0)){
				lock_release(&filesys_lock);
    			exit(-1);
    		}
    	}*/
    	
		size_write=file_write(searching_file,(const void*)buffer,size);
	}
	lock_release(&filesys_lock);
	return size_write;
}
void seek (int fd, unsigned position) { 
		/* search file using file director*/ 
	struct file *searching_file = process_get_file(fd);
	/* move the file offset as position */ 
	file_seek(searching_file, (off_t)position); 
} 
unsigned tell (int fd) { 
		/* search file using file director*/ 
	struct file *searching_file = process_get_file(fd);
	/* return the file location*/ 
	return file_tell(searching_file);
} 
void close (int fd) { 
	/* close file of fd and initiallize the fd entry */ 
	process_close_file(fd);  
} 