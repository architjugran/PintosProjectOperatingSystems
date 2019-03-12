#include "threads/palloc.h"
#include "threads/synch.h" 
#include "vm/frame.h"
#include <malloc.h>
struct list frame_table;
struct lock frame_table_lock;
void *
framealloc (enum palloc_flags flags)
{
  if (flags & PAL_USER == 0)
    return NULL;
  void *frame = palloc_get_page (flags);
  if (frame != NULL)
    return frame;
  else
    return NULL;
}
void
freeframe (void *frame)
{
  struct list_elem *e;
  struct frame_table_element *fte;

  // Search the frame table to find the frame and remove it from list 
  lock_acquire(&frame_table_lock);
  for (e = list_begin(&frame_table);
	e != list_end(&frame_table);e = list_next(e))
  {
    fte = list_entry(e, struct frame_table_element, elem);
    if (fte->frame == frame)
    {
      list_remove(&fte->elem);
      break;
    }
  }
  lock_release(&frame_table_lock);
  palloc_free_page (frame);
}

void
frame_table_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_table_lock);
}

static void
frame_table_insert (void *frame, struct spt_elem *spte) {

  // Get memory for frame element
  struct frame_table_element *fte = 
	(struct frame_table_element*) malloc(sizeof(struct frame_table_element));
  
  // insert current thread,frame and spte in frame element
  fte->t = thread_current();
  fte->frame = frame;
  fte->spte = spte;

  //Insert element in table
  lock_acquire(&frame_table_lock);
  list_push_back(&frame_table,&fte->elem);
  lock_release(&frame_table_lock);

}
