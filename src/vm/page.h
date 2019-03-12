#include <hash.h>
enum spte_type
{
    CODE = 0, 
    FILE = 1, 
    MMAP = 2  
};
struct spt_elem
{
    void *frame;
    void *upage;  
    enum spte_type type;
    struct hash_elem elem;
};

unsigned
spt_hash_func (const struct hash_elem *element, void *aux);
bool
comparator (const struct hash_elem *a, const struct hash_elem *b, void *aux);
void
supp_page_table_init ();
static struct spt_elem *
create_entry ();
void
create_entry_code (void *upage);
void
free_entry (struct hash_elem *e, void *aux);
void destroy_entry ();
