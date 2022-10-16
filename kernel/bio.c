// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUFMAP_BUCKET 13
#define HASH(number) (number % NBUFMAP_BUCKET)
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;
  struct buf bufmap[NBUFMAP_BUCKET];
  struct spinlock bufmap_locks[NBUFMAP_BUCKET];
} bcache;

void
binit(void)
{
  struct buf *b;
  for(int i = 0; i < NBUFMAP_BUCKET; i++){
    initlock(&bcache.bufmap_locks[i], "bcache_bufmap");
    bcache.bufmap[i].next = 0;
  }
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->last_use = 0;
    initsleeplock(&b->lock, "buffer");
    b->next = bcache.bufmap[0].next;
    bcache.bufmap[0].next = b;
  }


  initlock(&bcache.lock, "bcache");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  
  uint key = HASH(blockno);
  
  // Is the block already cached?
  acquire(&bcache.bufmap_locks[key]);
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  release(&bcache.bufmap_locks[key]);
  acquire(&bcache.lock);
  
  for(b = bcache.bufmap[key].next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      acquire(&bcache.bufmap_locks[key]);
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  int least_use = 0;
  struct buf *before_b = 0;
  int index = -1;
  int is_update = 0;
  for(int i = 0; i < NBUFMAP_BUCKET; i++){
    is_update = 0;
    acquire(&bcache.bufmap_locks[i]);

    for(b = &bcache.bufmap[i]; b->next; b = b->next){
      if(b->next->refcnt == 0){
        if(!before_b){
          least_use = b->next->last_use;
          before_b = b;
          is_update = 1;
        }
        else if(b->next->last_use < least_use){
          least_use = b->next->last_use;
          before_b = b;
          is_update = 1;
        }
      }
    }
    if(!is_update){
      release(&bcache.bufmap_locks[i]);
    }
    else{
      if(index != -1){
        release(&bcache.bufmap_locks[index]);
      }
      index = i;
    }
  }
  
  if(index != -1){
    b = before_b->next;
    before_b->next = b->next;
    release(&bcache.bufmap_locks[index]);
    
    acquire(&bcache.bufmap_locks[key]);
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
    
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    release(&bcache.bufmap_locks[key]);
    
    release(&bcache.lock);
    acquiresleep(&b->lock);
    return b;
  }

  panic("bget: no buffers");
}

// static struct buf*
// bget(uint dev, uint blockno)
// {
//   struct buf *b;

//   uint key = HASH(blockno);

//   acquire(&bcache.bufmap_locks[key]);

//   // Is the block already cached?
//   for(b = bcache.bufmap[key].next; b; b = b->next){
//     if(b->dev == dev && b->blockno == blockno){
//       b->refcnt++;
//       release(&bcache.bufmap_locks[key]);
//       acquiresleep(&b->lock);
//       return b;
//     }
//   }

//   // Not cached.

//   // to get a suitable block to reuse, we need to search for one in all the buckets,
//   // which means acquiring their bucket locks.
//   // but it's not safe to try to acquire every single bucket lock while holding one.
//   // it can easily lead to circular wait, which produces deadlock.

//   release(&bcache.bufmap_locks[key]);
//   // we need to release our bucket lock so that iterating through all the buckets won't
//   // lead to circular wait and deadlock. however, as a side effect of releasing our bucket
//   // lock, other cpus might request the same blockno at the same time and the cache buf for  
//   // blockno might be created multiple times in the worst case. since multiple concurrent
//   // bget requests might pass the "Is the block already cached?" test and start the 
//   // eviction & reuse process multiple times for the same blockno.
//   //
//   // so, after acquiring eviction_lock, we check "whether cache for blockno is present"
//   // once more, to be sure that we don't create duplicate cache bufs.
//   acquire(&bcache.lock);

//   // Check again, is the block already cached?
//   // no other eviction & reuse will happen while we are holding eviction_lock,
//   // which means no link list structure of any bucket can change.
//   // so it's ok here to iterate through `bcache.bufmap[key]` without holding
//   // it's cooresponding bucket lock, since we are holding a much stronger eviction_lock.
//   for(b = bcache.bufmap[key].next; b; b = b->next){
//     if(b->dev == dev && b->blockno == blockno){
//       acquire(&bcache.bufmap_locks[key]); // must do, for `refcnt++`
//       b->refcnt++;
//       release(&bcache.bufmap_locks[key]);
//       release(&bcache.lock);
//       acquiresleep(&b->lock);
//       return b;
//     }
//   }

//   // Still not cached.
//   // we are now only holding eviction lock, none of the bucket locks are held by us.
//   // so it's now safe to acquire any bucket's lock without risking circular wait and deadlock.

//   // find the one least-recently-used buf among all buckets.
//   // finish with it's corresponding bucket's lock held.
//   struct buf *before_least = 0; 
//   uint holding_bucket = -1;
//   for(int i = 0; i < NBUFMAP_BUCKET; i++){
//     // before acquiring, we are either holding nothing, or only holding locks of
//     // buckets that are *on the left side* of the current bucket
//     // so no circular wait can ever happen here. (safe from deadlock)
//     acquire(&bcache.bufmap_locks[i]);
//     int newfound = 0; // new least-recently-used buf found in this bucket
//     for(b = &bcache.bufmap[i]; b->next; b = b->next) {
//       if(b->next->refcnt == 0 && (!before_least || b->next->last_use < before_least->next->last_use)) {
//         before_least = b;
//         newfound = 1;
//       }
//     }
//     if(!newfound) {
//       release(&bcache.bufmap_locks[i]);
//     } else {
//       if(holding_bucket != -1) release(&bcache.bufmap_locks[holding_bucket]);
//       holding_bucket = i;
//       // keep holding this bucket's lock....
//     }
//   }
//   if(!before_least) {
//     panic("bget: no buffers");
//   }
//   b = before_least->next;
  
//   if(holding_bucket != key) {
//     // remove the buf from it's original bucket
//     before_least->next = b->next;
//     release(&bcache.bufmap_locks[holding_bucket]);
//     // rehash and add it to the target bucket
//     acquire(&bcache.bufmap_locks[key]);
//     b->next = bcache.bufmap[key].next;
//     bcache.bufmap[key].next = b;
//   }
  
//   b->dev = dev;
//   b->blockno = blockno;
//   b->refcnt = 1;
//   b->valid = 0;
//   release(&bcache.bufmap_locks[key]);
//   release(&bcache.lock);
//   acquiresleep(&b->lock);
//   return b;
// }

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int key = HASH(b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->last_use = ticks;
  }
  release(&bcache.bufmap_locks[key]);
}


void
bpin(struct buf *b) {
  int key = HASH(b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt++;
  release(&bcache.bufmap_locks[key]);
}

void
bunpin(struct buf *b) {
  int key = HASH(b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  release(&bcache.bufmap_locks[key]);
}


