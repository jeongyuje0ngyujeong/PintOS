#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/file.h"
#include "filesys/filesys.h"


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

// bool
// check_valid_fd(int fd){
//    for (int i = 0; i < 64; i++)
//    {
//       if (fd == i && thread_current()->fd_table[i] != NULL)
//          return true;
//    }
//    return false;
// }

// bool 
// check_valid_mem(void* ptr){
//    if (ptr == NULL || !is_user_vaddr (ptr) || pml4_get_page(thread_current()->pml4, ptr) == NULL)
//       return false;
//    return true;
// }

// void
// set_code_and_exit(int exit_code){
//    thread_current()->file_status = exit_code;
//    thread_exit();
// }

void
exit(struct intr_frame *f) {
	struct thread *curr = thread_current();
	// printf("current thread: %p\n", curr);
	curr->file_status = f->R.rdi;
	
	thread_exit();
}

void
create(struct intr_frame *f) {
	char *cur_file = f->R.rdi;
	printf("cur_file: %p\n",cur_file);
	if (cur_file == NULL)
	{
		thread_current()->file_status = -1;
		thread_exit();
		return;
	}
	
	unsigned size = f->R.rsi;
	f->R.rax = filesys_create (cur_file, size); 

}

void 
write(struct intr_frame *f) {
	int fd = f->R.rdi;
	const void *buffer = f->R.rsi;
	size_t size = f->R.rdx;

	if (fd == 1) {
		putbuf((char *)buffer, size);
		f->R.rax = size;

	} else {
		struct file *cur_file = thread_current()->fd_table[fd];
		size_t real_size = file_write(cur_file, (char *)buffer, size);
		f->R.rax = real_size;
	}
}

void
read(struct intr_frame *f) {
	int fd = f->R.rdi;
	void *buffer = f->R.rsi;
	size_t size = f->R.rdx;
	
	if (fd == 0 && buffer != NULL) {
		f->R.rax = input_getc();
		return;
	}
	else if (2 < fd && fd < 30 && buffer != NULL) {
		struct file *file = thread_current()->fd_table[fd];
		f->R.rax = file_read(file, (char *)buffer, size);
		return;	
	}
	else {
		thread_current()->file_status = -1;
		thread_exit();
		return;
	}
}

void
open(struct intr_frame *f) {
	struct thread *t = thread_current();
	char *file_name = f->R.rdi;

	
	// if (file_name == NULL || file_name[0] == '\0')
	if (file_name == NULL)
	{	
		thread_current()->file_status = -1;
		thread_exit();
		return;
	}

	struct file *cur_file = filesys_open(file_name);

	if (cur_file == NULL)
	{
		/* 	안 해도 됨
			thread_current()->file_status = -1; */
		f->R.rax = -1;
		return;
	}
	
	int i = 3;
	while (t->fd_table[i] != NULL)
		i++;
	
	t->fd_table[i] = cur_file;
	f->R.rax = i;
}

void
filesize(struct intr_frame *f) {
	int fd = f->R.rdi;
	if (fd < 3) {
	
		thread_current()->file_status = -1;
		f->R.rax = -1;

	} else {
		f->R.rax = file_length(thread_current()->fd_table[fd]);
		
	}
}

void
close(struct intr_frame *f) {
	int fd = f->R.rdi;
	if (!is_valid_fd(fd)) return;

	file_close(thread_current()->fd_table[fd]);
	thread_current()->fd_table[fd] = NULL;
	
	//else f->R.rax = -1;
}


/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// printf ("system call!\n");
	thread_current()->is_user = true;
	switch (f->R.rax)
	{
	case SYS_HALT:     			/* (0) Halt the operating system. */
		power_off();
		break;
	case SYS_EXIT:				/* (1) Terminate this process. */
		exit(f);
		break;                
	case SYS_FORK:              /* (2) Clone current process. */
		break;
	case SYS_EXEC:              /* (3) Switch current process. */
		break;
	case SYS_WAIT:              /* (4) Wait for a child process to die. */
		break;
	case SYS_CREATE:			/* (5) Create a file. */
		create(f);
		break;                 
	case SYS_REMOVE:            /* (6) Delete a file. */
		break;
	case SYS_OPEN:              /* (7) Open a file. */
		open(f);
		// {
		// char *file_name = f->R.rdi;
		// if (!check_valid_mem(file_name))
		// 	set_code_and_exit(-1);

		// struct file* open_file = filesys_open(file_name);
		
		// if (open_file == NULL)
		// 	f->R.rax = -1;
		// else
		// {
		// 	int i = 0;
		// 	while(thread_current()->fd_table[i])
		// 		i ++;
		// 	thread_current()->fd_table[i] = open_file;

		// 	f->R.rax = i;
		// }
		// break;
		// }
		break;
	case SYS_FILESIZE:			/* (8) Obtain a file's size. */
		filesize(f);
		break;               
	case SYS_READ:              /* (9) Read from a file. */
		read(f);
		break;
	case SYS_WRITE:				/* (10) Write to a file. */
		write(f);
		break;                  
	case SYS_SEEK:              /* (11) Change position in a file. */
		break;
	case SYS_TELL:				/* (12) Report current position in a file. */
		break;                   
	case SYS_CLOSE:
		close(f);
		// {
		// int fd = f->R.rdi;

		// if (!check_valid_fd(fd)) break;
		
		// struct file* close_file 
		// 		= thread_current()->fd_table[fd];
		
		// file_close(close_file);
		// thread_current()->fd_table[fd] = NULL;
	
		// break;
		// }
		break;
	
	
	default:
		break;
	}
	// thread_exit ();
}
