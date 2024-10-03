/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/vaddr.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

static bool
file_lazy_load_segment (struct page *page, void *aux) {
	struct file *file = ((struct load_segment_info *)aux)->file;
	off_t ofs = ((struct load_segment_info *)aux)->ofs;
	uint8_t *upage = ((struct load_segment_info *)aux)->upage;
	uint32_t read_bytes = ((struct load_segment_info *)aux)->read_bytes;
	uint32_t zero_bytes = ((struct load_segment_info *)aux)->zero_bytes;
	bool writable = ((struct load_segment_info *)aux)->writable;

	file_seek(file, ofs);
	/* Do calculate how to fill this page.
	* We will read PAGE_READ_BYTES bytes from FILE
	* and zero the final PAGE_ZERO_BYTES bytes. */
	size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;

 	/* Kernel page */
	void *kpage = page->frame->kva;

 	/* Load this page. */
	if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
		palloc_free_page (page);
		return false;
	}
	
	memset (kpage + page_read_bytes, 0, page_zero_bytes);
	return true;
	

}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {

	// printf("do mmap? \n");

	void *start_addr = addr;
	// printf("reopen file before \n");
	// printf("reopen file before addr: %p\n", file);
	struct file *reopen_file = file_reopen(file);
	// printf("re open file after\n");
	/* length에 사용하는 것임. 즉, length를 page size규격에 맞춰서
	 * 반올림하여 pgsize로 나누면 페이지가 몇 개 쓰이는 지 알 수 있음 */
	int page_cnt = (int)pg_round_up(length) / PGSIZE; 
	// printf("page cnt: %d\n", page_cnt);
	off_t file_size = file_length(reopen_file);

	while (page_cnt > 0) {
		struct load_segment_info *info = malloc(sizeof(struct load_segment_info));
		if (info == NULL) PANIC("TODO");

		size_t page_read_bytes = file_size < PGSIZE ? file_size : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		info->file = file; //데이터를 읽어올 파일.
		info->ofs = offset; //파일에서 데이터를 읽기 시작할 오프셋.
		info->upage = addr; //가상 메모리 주소.
		info->read_bytes = page_read_bytes; //읽어야 할 바이트 수.
		info->zero_bytes = page_zero_bytes; //0으로 채워야 할 바이트 수.
		info->writable = writable; //메모리가 쓰기 가능한지 여부.

		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, file_lazy_load_segment, info)) return NULL;

		page_cnt--;
		file_size -= PGSIZE;
		offset += page_read_bytes;
		addr += PGSIZE;
	}
	return start_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
}
