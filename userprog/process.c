#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"

#define VM
#define USERPROG

#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	char *token, *save_ptr;
	token = strtok_r (file_name, " ", &save_ptr);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (token, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();
	// print("init 잘됐니?\n");
	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	tid_t tid = thread_create (name,PRI_DEFAULT, __do_fork, thread_current ());
	if (tid == TID_ERROR) return -1;
	
	struct thread *curr = thread_current();
	
	sema_down(&curr->fork_sema);
	struct thread *child = get_thread_to_tid(tid);

	if (child->exit_status == -1) {
		for (int i = 0; i < CHILD_MAX; i++)
		{
			if (child == curr->childern[i]) curr->childern[i] = NULL;
			return -1;
		}	
	}
	
	return tid;
}
	

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va)) return true;
	
	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (parent_page == NULL) return TID_ERROR;

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);
	if (parent_page == NULL) return TID_ERROR;

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */

	writable = is_writable(pte);
	memcpy(newpage, parent_page, PGSIZE);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		palloc_free_page(newpage);
		return TID_ERROR;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	struct intr_frame *parent_if;

	
	parent_if = &parent->user_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	/* 자식이 성공적으로 만들어지고 있다,, */
	if_.R.rax = 0;

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;
	// printf("여기까지니??ㅇ?여가지강ㄱ라리리리리 \n");
	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* 
	 * Hint) To duplicate the file object, use `file_duplicate`
	 *        in include/filesys/file.h. Note that parent should not return
	 *        from the fork() until this function successfully duplicates
	 *        the resources of parent.*/

	for (int i = 3; i < FD_MAX; i++)
	{
		if (parent->fd_table[i] != NULL)
			current->fd_table[i] = file_duplicate(parent->fd_table[i]);
	}

	process_init ();
	sema_up(&parent->fork_sema);


	/* Finally, switch to the newly created process. */
	if (succ) 
		do_iret (&if_);


error:
	sema_up(&parent->fork_sema);
	current->parent_thread = NULL;
	thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();

	/* And then load the binary */
	success = load (file_name, &_if);
	// printf("load success: %d\n", success);

	/* If load failed, quit. */
	palloc_free_page (file_name);
	if (!success)
		return -1;

	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);
	printf("안녕 프로세스잌ㅅ\n");
	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	struct thread *child = get_thread_to_tid(child_tid);

	if (child == NULL) return -1;
	
	sema_down(&child->wait_sema);
	
	struct thread *parent = child->parent_thread;
	if (parent != thread_current())
		return -1;
	
	bool is_child = false;

	for (int i = 0; i < CHILD_MAX; i++) {
		if (parent->childern[i] == child) {
			is_child = true;
			parent->childern[i] = NULL;
		}
	}
	
	if (!is_child) {
		child->exit_status = -1;
		return -1;
	}

	sema_up(&child->free_sema);
		
	return child->exit_status;

}

/* Exit the process. This function is called by thread_exit (). */
void
 process_exit (void) {
	struct thread *curr = thread_current ();
	if (curr->is_user) {
		printf("%s: exit(%d)\n", curr->name, curr->exit_status);

		if (thread_current()->parent_thread != NULL){
			sema_up(&curr->wait_sema);

			for (int i = 3; i < 30; i++) {
				if (curr->fd_table[i] != NULL) {
					file_close(curr->fd_table[i]);
					curr->fd_table[i] = NULL;
				}
			}
			// printf("죽기직전 마지막 세마하기,,,직전,,,\n");

			/* child는 yield가 되어 바로 sema_up 후 sema down 이하 코드가 실행되는 것은 아님! 죽기직전에 모든걸 다 맞치고 sema down 실행해야 함 */
			if (curr->exec_file != NULL) {
				file_close(curr->exec_file);
				curr->exec_file == NULL;
			}

			sema_down(&curr->free_sema);
		} else {
			process_cleanup();
		}
	}
	process_cleanup ();
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	/* 테스크 전환에 필요한 정보들을 저장하고 전환 시 사용되는 구조체 : struct task state (tss)  */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	char argument[LOADER_ARGS_LEN];
	char *argv[LOADER_ARGS_LEN/2 + 1];
	char *token, *save_ptr;
	int argc = 0;
	
	/* 추후 사용 가능성이 있을 수도 있기에 확장성을 위하여 */
	strlcpy(argument, file_name, LOADER_ARGS_LEN);  
	
	for (token = strtok_r (argument, " ", &save_ptr); token != NULL; token = strtok_r (NULL, " ", &save_ptr))
	{
		argv[argc] = token;
		argc ++;
	}
	argv[argc] = NULL;    

	/* Open executable file. */
	file = filesys_open (argv[0]);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
			/* Ignore this segment. */
			break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
			goto done;
			case PT_LOAD:
			if (validate_segment (&phdr, file)) {
				bool writable = (phdr.p_flags & PF_W) != 0;
				uint64_t file_page = phdr.p_offset & ~PGMASK;
				uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
				uint64_t page_offset = phdr.p_vaddr & PGMASK;
				uint32_t read_bytes, zero_bytes;
				if (phdr.p_filesz > 0) {
					/* Normal segment.
					* Read initial part from disk and zero the rest. */
					read_bytes = page_offset + phdr.p_filesz;
					zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
						- read_bytes);
				} else {
					/* Entirely zero.
					* Don't read anything from disk. */
					read_bytes = 0;
					zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
				}
				// printf("load 잘 되고 있니,,? load segment 이전\n");
				if (!load_segment (file, file_page, (void *) mem_page,
						read_bytes, zero_bytes, writable)){
					// printf("load_segemnt 실패했니..?\n");
					goto done;
				}
			}
			else
				goto done;
			break;
		}
	}
	// printf("민경이 똥\n");

	/* Set up stack. */
	if (!setup_stack (if_)){
		printf("setup_stack fail?\n");
		goto done;
	}
	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* Your code goes here.
	 * Implement argument passing (see project2/argument_passing.html). */

	for (i = argc-1; i >= 0; i--){
		if_->rsp -= strlen(argv[i]) + 1;
		// printf("여기가 오류임?11111\n");
		strlcpy((char*)if_->rsp, argv[i], strlen(argv[i])+1);
		argv[i] = (char*)if_->rsp;
	}

	if_->rsp = ROUND_DOWN(if_->rsp, 8);

	for (i = argc; i >= 0; i--){
		if_->rsp -= sizeof(argv[i]);
		*(uintptr_t*)if_->rsp = (uintptr_t)argv[i];
	}

	if_->rsp -= sizeof(void*);

	// hex_dump(if_->rsp, if_->rsp, USER_STACK - if_->rsp, true);

	if_->R.rsi = if_->rsp + 8;
	if_->R.rdi = argc;

	success = true;

	t->exec_file = file;
	file_deny_write(t->exec_file);

done:
	/* We arrive here whether the load is successful or not. */
	if (!success)
		file_close (file);
	return success;
}

/* 유정 코드 */
// static bool
// load (const char *file_name, struct intr_frame *if_) {
// 	struct thread *t = thread_current ();
// 	struct ELF ehdr;
// 	struct file *file = NULL;
// 	off_t file_ofs;
// 	bool success = false;
// 	int i;

// 	char *token;
// 	char *argv[128];
// 	char *save_ptr;
// 	int argc = 0;

// 	for (token = strtok_r (file_name, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr))
// 	{	
// 		argv[argc] = token;
// 		argc++;
// 	}


// 	/* Allocate and activate page directory. */
// 	t->pml4 = pml4_create ();
// 	if (t->pml4 == NULL)
// 		goto done;
// 	process_activate (thread_current ());

// 	/* Open executable file. */
// 	file = filesys_open (argv[0]);
// 	// file = filesys_open(file_name);
// 	if (file == NULL) {
// 		printf ("load: %s: open failed\n", file_name);
// 		goto done;
// 	}

// 	/* Read and verify executable header. */
// 	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
// 			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
// 			|| ehdr.e_type != 2
// 			|| ehdr.e_machine != 0x3E // amd64
// 			|| ehdr.e_version != 1
// 			|| ehdr.e_phentsize != sizeof (struct Phdr)
// 			|| ehdr.e_phnum > 1024) {
// 		printf ("load: %s: error loading executable\n", file_name);
// 		goto done;
// 	}

// 	/* Read program headers. */
// 	file_ofs = ehdr.e_phoff;
// 	for (i = 0; i < ehdr.e_phnum; i++) {
// 		struct Phdr phdr;

// 		if (file_ofs < 0 || file_ofs > file_length (file))
// 			goto done;
// 		file_seek (file, file_ofs);

// 		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
// 			goto done;
// 		file_ofs += sizeof phdr;
// 		switch (phdr.p_type) {
// 			case PT_NULL:
// 			case PT_NOTE:
// 			case PT_PHDR:
// 			case PT_STACK:
// 			default:
// 				/* Ignore this segment. */
// 				break;
// 			case PT_DYNAMIC:
// 			case PT_INTERP:
// 			case PT_SHLIB:
// 				goto done;
// 			case PT_LOAD:
// 				if (validate_segment (&phdr, file)) {
// 					bool writable = (phdr.p_flags & PF_W) != 0;
// 					uint64_t file_page = phdr.p_offset & ~PGMASK;
// 					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
// 					uint64_t page_offset = phdr.p_vaddr & PGMASK;
// 					uint32_t read_bytes, zero_bytes;
// 					if (phdr.p_filesz > 0) {
// 						/* Normal segment.
// 						 * Read initial part from disk and zero the rest. */
// 						read_bytes = page_offset + phdr.p_filesz;
// 						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
// 								- read_bytes);
// 					} else {
// 						/* Entirely zero.
// 						 * Don't read anything from disk. */
// 						read_bytes = 0;
// 						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
// 					}
// 					if (!load_segment (file, file_page, (void *) mem_page,
// 								read_bytes, zero_bytes, writable))
// 						goto done;
// 				}
// 				else
// 					goto done;
// 				break;
// 		}
// 	}

// 	/* Set up stack. */
// 	if (!setup_stack (if_))
// 		goto done;

// 	/* Start address. */
// 	if_->rip = ehdr.e_entry;

// 	uintptr_t addr[(128 / 2) + 1];
// 	for (int i = argc - 1; i >= 0; i--)
// 	{
// 		if_->rsp -= strlen(argv[i]) + 1;
// 		strlcpy(if_->rsp, argv[i], strlen(argv[i]) + 1);
// 		addr[i] = if_->rsp;
// 	}

// 	size_t ptr_size = 8;
// 	if_->rsp = ROUND_DOWN(if_->rsp, ptr_size);

// 	if_->rsp -= ptr_size;
// 	memset(if_->rsp, 0, ptr_size);

// 	for (int i = argc - 1; i >= 0; i--)
// 	{
// 		if_->rsp -= ptr_size;
// 		*(uintptr_t *)if_->rsp = addr[i];		
// //		memcpy(if_->rsp, addr[i], ptr_size); //이 새끼 문제였음
// 	}

// 	if_->rsp = if_->rsp - ptr_size;
// 	memset(if_->rsp, 0, ptr_size);


// 	if_->R.rsi = if_->rsp + ptr_size;
// 	if_->R.rdi = argc;
	
// 	success = true;

// done:
// 	/* We arrive here whether the load is successful or not. */
// 	file_close (file);
// 	return success;
// }


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
/* 하나의 페이지를 로드하기 위한 함수야 근데 page fault가 날 때, */
/* lazy_load_segment가 실행되면서 load_segment() 일과 같은 */
/* 일을 실행하는 거야                                     */

	struct load_segment_info *info = aux;

	struct file *file = info->file;
	off_t ofs = info->ofs;
	uint8_t *upage = info->upage;
	uint32_t read_bytes = info->read_bytes;
	uint32_t zero_bytes = info->zero_bytes;
	bool writable = info->writable;

	printf("read_bytes: %d\n", read_bytes);

	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek(file, ofs);

	/* Kernel page */
	void *kpage = page->frame->kva;

	/* Load this page. */
	if (file_read (file, kpage, read_bytes) != (int) read_bytes) {
		palloc_free_page (kpage);
		return false;
	}
	
	memset (kpage + read_bytes, 0, zero_bytes);

	return true;
	

}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	/* load_segment_info init */	

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* page가 생성될 때마다 malloc 해줘야 함 */
		struct load_segment_info *info = malloc(sizeof(struct load_segment_info));
		if (info == NULL) PANIC("TODO");
	
		info->file = file; //데이터를 읽어올 파일.
		info->ofs = ofs; //파일에서 데이터를 읽기 시작할 오프셋.
		info->upage = upage; //가상 메모리 주소.
		info->read_bytes = page_read_bytes; //읽어야 할 바이트 수.
		info->zero_bytes = page_zero_bytes; //0으로 채워야 할 바이트 수.
		info->writable = writable; //메모리가 쓰기 가능한지 여부.

		/* Set up aux to pass information to the lazy_load_segment. */
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, info)) {
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
		// printf("load_segment에서의 readbytes: %d\n", info->read_bytes);
		// printf("load_segment에서의 zerobytes: %d\n", info->zero_bytes);
	}

	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* Map the stack on stack_bottom and claim the page immediately.
	 * If success, set the rsp accordingly.
	 * You should mark the page is stack. */
	/* Your code goes here */
	printf("alloc ? %d\n", vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, true));
	// if (!vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, true)) return false;
	if (!vm_claim_page(stack_bottom)){
		return false;	
	} 
	if_->rsp = USER_STACK;

	return true;
}
#endif /* VM */
