// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.


struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  int ref_counter[(PHYSTOP - KERNBASE) / PGSIZE];
} kmem;

int page_index(uint64 pa){
  pa = PGROUNDDOWN(pa);
  int res = (pa-(uint64)end) / PGSIZE;
  if(res < 0 || res > ((PHYSTOP - (uint64)end) / PGSIZE));
  return res;
}

void incr_cnt(uint64 pa){
  int index = page_index(pa);
  acquire(&kmem.lock);
  kmem.ref_counter[index] ++;
  
  release(&kmem.lock);
}

void decr_cnt(uint64 pa){
  int index = page_index(pa);
  acquire(&kmem.lock);
  kmem.ref_counter[index] --;
  
  release(&kmem.lock);
}

void
kinit()
{
  // printf("page_count:%d\n",(PHYSTOP - KERNBASE) / PGSIZE);
  // printf("end: %p, %d\n", end, (PHYSTOP - (uint64)end) / PGSIZE);
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  acquire(&kmem.lock);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    kmem.ref_counter[page_index((uint64)p)] = 1;
    // if((uint64)p < (uint64)pa_start + 10 * PGSIZE)
    // printf("%d,%d\n",page_index((uint64)p), kmem.ref_counter[page_index((uint64)p)]);
  }
  release(&kmem.lock);
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    
    kfree(p);
    
  }

}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  decr_cnt((uint64)pa);
  
  if (kmem.ref_counter[page_index((uint64)pa)] < 0)
  {
    //printf("%d,%d\n",page_index((uint64)pa), kmem.ref_counter[page_index((uint64)pa)]);
    panic(" ref_counter < 0\n");
  }
  if (kmem.ref_counter[page_index((uint64)pa)] == 0)
  {
    //printf("%d,%d\n",page_index((uint64)pa), kmem.ref_counter[page_index((uint64)pa)]);
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run *)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  
}

void print_free_pages(){
  struct run *r;
  int cnt = 0;
  acquire(&kmem.lock);
  r = kmem.freelist;
  while(r->next){
    r = r->next;
    cnt++;
  }
    
  release(&kmem.lock);
  printf("free page: %d\n",cnt);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    
    acquire(&kmem.lock);
    kmem.ref_counter[page_index((uint64)r)] = 1;
    //printf("%d,%d\n",page_index((uint64)r), kmem.ref_counter[page_index((uint64)r)]);
    //printf("%d : %d\n",((uint64)r - (uint64)end) / PGSIZE,kmem.ref_counter[((uint64)r - (uint64)end) / PGSIZE]);
    release(&kmem.lock);
    //printf("ref_count[%d]\n = 1,",((uint64)r - (uint64)end) / PGSIZE);
    //print_free_pages();
  }
    
  return (void*)r;
}

int is_single_page(uint64 pa){
  if(kmem.ref_counter[page_index(pa)] == 1){
    return 1;
  }
  return 0;
}