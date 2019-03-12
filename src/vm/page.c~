#include "vm/page.h"
#include "threads/synch.h"
struct hash supp_page_table;
struct lock spt_lock;

unsigned
spt_hash_func (const struct hash_elem *element, void *aux)
{
  struct spt_elem *spte = hash_entry (element, struct spt_elem, elem);
  return hash_int ((int) spte->upage);
}

bool
comparator (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
  struct spt_elem *sptea = hash_entry (a, struct spt_elem, elem);
  struct spt_elem *spteb = hash_entry (b, struct spt_elem, elem);
  return (int) sptea->upage < (int) spteb->upage;
}

void
supp_page_table_init ()
{
  hash_init (&supp_page_table, spt_hash_func, comparator, NULL);
  lock_init (&spt_lock);
}

static struct spt_elem *
create_entry ()
{
  struct spt_elem *spte = (struct spt_elem *) malloc (
    sizeof (struct spt_elem));
  spte->frame = NULL;
  spte->upage = NULL;
  return spte;
}

void
create_entry_code (void *upage)
{
  struct spt_elem *spte = create_entry ();
  spte->type = CODE;
  spte->upage = upage;
  lock_acquire (&spt_lock);
  hash_insert (&supp_page_table, &spte->elem);
  lock_release (&spt_lock);
}

void
free_entry (struct hash_elem *e, void *aux)
{
  struct spt_elem *spte = hash_entry (e, struct spt_elem, elem);
  if (spte->frame != NULL)
    freeframe (spte->frame);
  free (spte);
}

void destroy_entry (){
  lock_acquire (&spt_lock);
  hash_destroy (&supp_page_table, free_entry);
  lock_release (&spt_lock);
}
