#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);

// void open(struct intr_frame *f);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

bool
is_valid_fd (int fd) {
	if (2 < fd && fd < 30 && thread_current()->fd_table[fd] != NULL) return true;
	return false;	
}

void
exit(int status) {
	struct thread *curr = thread_current();
	curr->exit_status = status;
	// sema_up(curr->parent_thread->wait_sema);

	thread_exit();
}

bool
create(char *cur_file, size_t size) {
	if (cur_file == NULL || !is_user_vaddr(cur_file) || !pml4_get_page(thread_current()->pml4, cur_file))
	{
		thread_current()->exit_status = -1;
		thread_exit();
		return;
	}
	return filesys_create (cur_file, size); 
}

int 
write(int fd, void *buffer, size_t size) {
	struct thread *curr = thread_current();

	if (fd == 1) {
		putbuf((char *)buffer, size);
		return size;

	} else if (is_valid_fd(fd)) {
		struct file *cur_file = thread_current()->fd_table[fd];
		size_t real_size = file_write(cur_file, (char *)buffer, size);
		return real_size;
	}
}

int
read(int fd, void *buffer, size_t size) {
	if (fd == 0 && buffer != NULL) return input_getc();
	else if (2 < fd && fd < 30 && buffer != NULL) 
	{
		struct file *file = thread_current()->fd_table[fd];
		return file_read(file, (char *)buffer, size);
	}
	else 
	{
		thread_current()->exit_status = -1;
		thread_exit();
		return;
	}
}

tid_t
fork (char *thread_name, struct intr_frame *user_frame){
	memcpy(&thread_current()->user_if, user_frame, sizeof(struct intr_frame));

	/* fork를 통해 복제된 thread와 thread 생성시 만들어진 child는 다른데..ㅠㅠㅠ */

	tid_t child_tid = process_fork (thread_name, user_frame);
	if (child_tid == -1) return TID_ERROR;

	return child_tid;
}

int
open(char *file_name) {
	struct thread *t = thread_current();

	if (file_name == NULL || !is_user_vaddr(file_name) || !pml4_get_page(thread_current()->pml4, file_name))
	{	
		thread_current()->exit_status = -1;
		thread_exit();
		
	}

	struct file *cur_file = filesys_open(file_name);
	
	if (cur_file == NULL) return -1;
	
	int i = 3;
	while (t->fd_table[i] != NULL && i < FD_MAX) 
		i++;
	
	if (i < FD_MAX) {
		t->fd_table[i] = cur_file;
		return i;
	}
	
	file_close(cur_file);
	return -1;
}

int
filesize (int fd) {
	if (fd < 3) {
		thread_current()->exit_status = -1;
		return -1;
	} 
	else return file_length(thread_current()->fd_table[fd]);	
	
}

void
exec(char *cmd_line, struct intr_frame *f){
	/* address가 유효한 address인지 확인하는 코드 */
	if (cmd_line == NULL || !is_user_vaddr(cmd_line) || !pml4_get_page(thread_current()->pml4, cmd_line))
	{	
		thread_current()->exit_status = -1;
		thread_exit();
		
	}

	/* pid */
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
		exit(-1);

	strlcpy(fn_copy, cmd_line, PGSIZE);

	// printf("process exec 실행한다1!!\n");
	if (process_exec(fn_copy) < 0) {
		f->R.rax = -1;

		thread_current()->exit_status = -1;
		thread_exit();
	}
}

int
wait(tid_t tid) {
	return process_wait(tid);
}

bool
remove (char *file_name) {
	if (file_name == NULL || !is_user_vaddr(file_name) || pml4_get_page(thread_current()->pml4, file_name) == NULL)
		return false;
	
	return filesys_remove(file_name);
}

void
seek(int fd, size_t position) {
	struct file *file = thread_current()->fd_table[fd];
	file_seek(file, position);
}

off_t
tell(int fd) {
	struct file *file = thread_current()->fd_table[fd];
	return file_tell(file);
}

void
close(int fd) {
	if (!is_valid_fd(fd)) return;

	file_close(thread_current()->fd_table[fd]);
	thread_current()->fd_table[fd] = NULL;

}


/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	thread_current()->is_user = true;

	switch (f->R.rax) {
	case SYS_HALT:     			/* (0) Halt the operating system. */
	{
		power_off();
		break;
	}
	case SYS_EXIT:				/* (1) Terminate this process. */
	{
		int status = f->R.rdi;
		exit(status);
		break;
	} 

	case SYS_FORK:              /* (2) Clone current process. */
	{
		char *thread_name = f->R.rdi;
		f->R.rax = fork(thread_name, f);
		break;
	}

	case SYS_EXEC:              /* (3) Switch current process. */
	{
		char *file = f->R.rdi;
		exec(file, f);
		break;
	}
	case SYS_WAIT:              /* (4) Wait for a child process to die. */
	{
		tid_t tid = f->R.rdi;
		f->R.rax = wait(tid);
		break;
	}
	case SYS_CREATE:			/* (5) Create a file. */
	{
		char *cur_file = f->R.rdi;
		size_t size = f->R.rsi;
		f->R.rax = create(cur_file, size);
		break; 
	}     

	case SYS_REMOVE:            /* (6) Delete a file. */
	{
		char *file = f->R.rdi;
		f->R.rax = remove(file);
		break;
	}

	case SYS_OPEN:              /* (7) Open a file. */
	{
		char *file_name = f->R.rdi;
		f->R.rax = open(file_name);
		break;
	}

	case SYS_FILESIZE:			/* (8) Obtain a file's size. */
	{
		int fd = f->R.rdi;
		f->R.rax = filesize(fd);
		break;   
	}   

	case SYS_READ:              /* (9) Read from a file. */
	{
		int fd = f->R.rdi;
		void *buffer = f->R.rsi;
		size_t size = f->R.rdx;	
	 	f->R.rax = read(fd, buffer, size);
		break;
	}

	case SYS_WRITE:				/* (10) Write to a file. */
	{
		int fd = f->R.rdi;
		const void *buffer = f->R.rsi;
		size_t size = f->R.rdx;
		f->R.rax = write(fd, buffer, size);
		break;  
	}    

	case SYS_SEEK:              /* (11) Change position in a file. */
	{
		int fd = f->R.rdi;
		size_t position = f->R.rsi;
		seek(fd, position);
		break;
	}
	case SYS_TELL:				/* (12) Report current position in a file. */
	{
		int fd = f->R.rdi;
		f->R.rax = tell(fd);
		break;  
	}
	case SYS_CLOSE:
	{
		int fd = f->R.rdi;
		close(fd);
		break;
	}

	default:
		break;
	}
}
