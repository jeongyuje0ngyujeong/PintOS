/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "hash.h"
#include "threads/mmu.h"

#include "vm/uninit.h"
#include <string.h>

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
// bool
// vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
//       vm_initializer *init, void *aux) {
//    // printf("alloc page addr: %p\n", upage);
//    ASSERT (VM_TYPE(type) != VM_UNINIT)

//    struct supplemental_page_table *spt = &thread_current ()->spt;
//    /* Check wheter the upage is already occupied or not. */
//    if (spt_find_page (spt, upage) == NULL) {
//       /* TODO: Create the page, fetch the initialier according to the VM type,
//        * TODO: and then create "uninit" page struct by calling uninit_new. You
//        * TODO: should modify the field after calling the uninit_new. */
//       struct page* new_page = malloc(sizeof(struct page));

//       if (VM_TYPE(type) == VM_ANON){
//          uninit_new(new_page, upage, init, type, aux, anon_initializer);
//       }
//       else {
//          uninit_new(new_page, upage, init, type, aux, file_backed_initializer);
//       }
      
//       /* TODO: Insert the page into the spt. */
//       new_page->writable = writable;
//       if (spt_insert_page(spt, new_page)){
//          return true;
//       };
//    }
// err:
//    return false;
// }
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
	
	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* Create the page, fetch the initialier according to the VM type,
		 * and then create "uninit" page struct by calling uninit_new. You
		 * should modify the field after calling the uninit_new. */
		/* Insert the page into the spt. */
		
		/* page initalizer */
		struct page *page = malloc(sizeof(struct page));
		if (page == NULL) PANIC("TODO");

		bool (*initializer)(struct page *, enum vm_type, void *kva) = NULL;
		// bool* initializer = NULL;

		switch (VM_TYPE(type)) {
		case VM_ANON:
			// uninit_new(page, pg_round_down(upage), init, type, aux, anon_initializer);
			initializer = &anon_initializer;
			break;

		case VM_FILE:
			// uninit_new(page, pg_round_down(upage), init, type, aux, file_backed_initializer);
			initializer = &file_backed_initializer;
			break;

		default:
			break;
		}
		
		uninit_new(page, pg_round_down(upage), init, type, aux, initializer);
		page->writable = writable;
		spt_insert_page(spt, page);
		// printf("alloc addr: %p\n", pg_round_down(upage));
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
// struct page *
// page_lookup (struct hash *h UNUSED, const void *address) {
//   struct page p;
//   struct hash_elem *e;

//   p.va = address;
//   e = hash_find (h, &p.hash_elem);
//   return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
// }

/* Find VA from spt and return page. On error, return NULL. */
// struct page *
// spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
//    struct page *page = NULL;
//    /* TODO: Fill this function. */
//    page = page_lookup(&spt->hash, va);

//    return page;
// }
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	/* 더미 page를 설정해서 va값만 일단 대충 넣어야 하기 때문에 주소값이 아닌 그 자체로 저장 */
	struct page page;
	struct hash_elem *hash_elem;

	/* pg_round_down을 이용하여 page.va에 넣어야 하는 이유:
	 * stack은 아래에서 위로 자라남
	 * 인자로 넘어온 va는 page의 시작값이 아니라 중간값일 수 있음.
	 * 따라서 시작값, 즉 va가 속해있는 page의 주소값을 찾기(받기) 위해 pg_round_down을 시작해야함 
	*/
	page.va = pg_round_down(va);
	hash_elem = hash_find(&spt->hash, &page.hash_elem); 

	return hash_elem != NULL ? hash_entry(hash_elem, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int is_succ = false;
	if (spt_find_page(spt, page->va) != NULL) return is_succ;

	struct hash_elem *result = hash_insert(&spt->hash, &page->hash_elem);
	if (result != NULL) return is_succ;
	
	is_succ = true;
	return is_succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
// static struct frame *
// vm_get_frame (void) {
//    struct frame *frame = NULL;
//    /* TODO: Fill this function. */
//    void *kpage = palloc_get_page(PAL_USER);
//    if (kpage){
//       frame = malloc(sizeof(struct frame));
//       frame->kva = kpage;
//       frame->page = NULL;
//    }
//    else{
//       PANIC ("todo");
//    }

//    ASSERT (frame != NULL);
//    ASSERT (frame->page == NULL);
//    return frame;
// }
static struct frame *
vm_get_frame (void) {
	struct frame *frame = malloc(sizeof(struct frame));

	/* frame의 물리주소를 할당할 것이므로 frame kva를 palloc get page 해줘야 함*/
	frame->kva = palloc_get_page(PAL_USER|PAL_ZERO);
	if (frame->kva == NULL) PANIC("TODO");

	/* page는 가상주소공간에서 할당 받을 것이므로 NULL로 초기화 해줘야 함 */
	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */

bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {

	if (!not_present) {
		return false;
	}

	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = spt_find_page(spt, addr);
	if (page == NULL) return false;
	// printf("page->va: %p\n", page->va);

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
// bool
// vm_claim_page (void *va UNUSED) {
//    struct page *page = NULL;
//    /* TODO: Fill this function */
//    // page = malloc(sizeof(struct page));
//    // page->va = va;
//    page = spt_find_page(&thread_current()->spt, va);

//    return vm_do_claim_page (page);
// }
bool
vm_claim_page (void *va UNUSED) {
	/* Fill this function */
	struct page *page = spt_find_page(&thread_current()->spt, va);

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
// static bool
// vm_do_claim_page (struct page *page) {
//    struct frame *frame = vm_get_frame ();

//    /* Set links */
//    frame->page = page;
//    page->frame = frame;

//    /* TODO: Insert page table entry to map page's VA to frame's PA. */
//    pml4_set_page(thread_current()->pml4, page->va,frame->kva,page->writable);

//    // struct supplemental_page_table *spt = &thread_current()->spt;
//    // spt_insert_page(spt,page);

//    return swap_in (page, frame->kva);
// }
static bool
vm_do_claim_page (struct page *page) {
	//printf("어디에서 실패임?\n");
	struct frame *frame = vm_get_frame ();
	// printf("%p\n",frame);
	//printf("여기에도 안들어오지?\n");
	/* Set links */
	frame->page = page;
	page->frame = frame;
	/* Insert page table entry to map page's VA to frame's PA. */
	if (!pml4_set_page (thread_current()->pml4, page->va, frame->kva, page->writable)){
		// printf("여기에도 안들어오지?\n");
		return false;
	}	

	//if (page->operations->swap_in != NULL) printf("swap_in: %p\n", page->operations->swap_in );
	return swap_in (page, frame->kva);
}

// unsigned
// page_hash (const struct hash_elem *p_, void *aux UNUSED) {
//    const struct page *p = hash_entry (p_, struct page, hash_elem);
//    return hash_bytes (&p->va, sizeof p->va);
// }

// bool
// page_less (const struct hash_elem *a_,
//            const struct hash_elem *b_, void *aux UNUSED) {
//    const struct page *a = hash_entry (a_, struct page, hash_elem);
//    const struct page *b = hash_entry (b_, struct page, hash_elem);

//    return a->va < b->va;
// }

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->hash, page_hash, page_less, NULL);

}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {
		
	struct hash *hash = &src->hash;
	struct hash_iterator i;

	hash_first(&i, hash);
	while (hash_next(&i)) {
		struct page *page = hash_entry(hash_cur(&i), struct page, hash_elem);
		if (!spt_insert_page(dst, page)) return false;

	}
	return true;

}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* Destroy all the supplemental_page_table hold by thread and
	 * writeback all the modified contents to the storage. */

	struct hash *hash = &spt->hash;
	struct hash_iterator i;

	hash_first(&i, hash);
	while (hash_next(&i)) {
		struct page *page = hash_entry(hash_cur(&i), struct page, hash_elem);
		hash_destroy(hash, page_destructor);

	}
	return true;
}
