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

#define BUCKETSIZE 13
#define BUFFERSIZE 7

struct {
  struct spinlock lock;
  struct buf buf[BUFFERSIZE];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;
} bcachebucket[BUCKETSIZE];

void
binit(void)
{
  for(int i=0;i<BUCKETSIZE;i++){
    initlock(&bcachebucket[i].lock,"bcachebucket");
    for(int j=0;j<BUFFERSIZE;j++){
      initsleeplock(&bcachebucket[i].buf[j].lock,"buffer");
      bcachebucket[i].buf[j].lastuse=0;
      bcachebucket[i].buf[j].refcnt=0;
    }
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcachebucket[blockno%BUCKETSIZE].lock);

  // Is the block already cached?
  for(int i=0;i<BUFFERSIZE;i++){
    b=&bcachebucket[blockno%BUCKETSIZE].buf[i];
    if(b->dev==dev&&b->blockno==blockno){
      b->refcnt++;
      b->lastuse=ticks;
      release(&bcachebucket[blockno%BUCKETSIZE].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  //b=NULL;
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  uint least = 0xffffffff;//max number
  int least_idx = -1;
  for (int i = 0; i < BUFFERSIZE; i++) {
    b = &bcachebucket[blockno%BUCKETSIZE].buf[i];
    if(b->refcnt == 0 && b->lastuse < least) {
      least = b->lastuse;
      least_idx = i;
    }
  }
  if (least_idx == -1) {
    //b=NULL;
    //去邻居bucket偷取空闲buffer
    panic("do not have enough free buffer");
//    for(int i=1;i<BUCKETSIZE;i++){
//      uint next_bucket=(blockno+1)%BUCKETSIZE;
//      b=bget(dev,next_bucket);
//      if(b==null){
//        b->dev = dev;
//        b->blockno = blockno;
//        b->lastuse = ticks;
//        b->valid = 0;
//        b->refcnt = 1;
//        release(&bcachebucket[blockno%BUCKETSIZE].lock);
//        acquiresleep(&b->lock);
//        return b;
//      }
//    }
  }

  b = &bcachebucket[blockno%BUCKETSIZE].buf[least_idx];
  b->dev = dev;
  b->blockno = blockno;
  b->lastuse = ticks;
  b->valid = 0;
  b->refcnt = 1;
  release(&bcachebucket[blockno%BUCKETSIZE].lock);
  acquiresleep(&b->lock);
  // --- end of critical session
  return b;
}

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

  acquire(&bcachebucket[b->blockno%BUCKETSIZE].lock);
  b->refcnt--;
  release(&bcachebucket[b->blockno%BUCKETSIZE].lock);
  releasesleep(&b->lock);
}

void
bpin(struct buf *b) {
  acquire(&bcachebucket[b->blockno%BUCKETSIZE].lock);
  b->refcnt++;
  release(&bcachebucket[b->blockno%BUCKETSIZE].lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcachebucket[b->blockno%BUCKETSIZE].lock);
  b->refcnt--;
  release(&bcachebucket[b->blockno%BUCKETSIZE].lock);
}


