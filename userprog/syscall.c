#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	printf ("system call!\n");

	switch (f->R.rax)
	{
	case SYS_HALT:     			/* (0) Halt the operating system. */
		break;
	case SYS_EXIT:				/* (1) Terminate this process. */
		break;                  
	case SYS_FORK:              /* (2) Clone current process. */
		break;
	case SYS_EXEC:              /* (3) Switch current process. */
		break;
	case SYS_WAIT:              /* (4) Wait for a child process to die. */
		break;
	case SYS_CREATE:			/* (5) Create a file. */
		break;                 
	case SYS_REMOVE:            /* (6) Delete a file. */
		break;
	case SYS_OPEN:              /* (7) Open a file. */
		break;
	case SYS_FILESIZE:			/* (8) Obtain a file's size. */
		break;               
	case SYS_READ:              /* (9) Read from a file. */
		break;
	case SYS_WRITE:				/* (10) Write to a file. */
		break;                  
	case SYS_SEEK:              /* (11) Change position in a file. */
		break;
	case SYS_TELL:				/* (12) Report current position in a file. */
		break;                   
	case SYS_CLOSE:
		break;
	
	
	default:
		break;
	}
	thread_exit ();
}
