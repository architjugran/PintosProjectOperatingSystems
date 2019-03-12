#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <string.h>
#include "threads/synch.h"
#include "userprog/process.h"

static struct lock file_lock;
static void syscall_handler (struct intr_frame *);
static void checkvalidstring (const char *);
static void checkvalid (const void *ptr,size_t);
static void close_file (int);
static int checkfd (int fd);

static int
halt (void *esp)
{
  power_off();
}

int
exit (void *esp)
{
  int status = 0;
  if (esp != NULL){
    checkvalid (esp, sizeof(int));
    status = *((int *)esp);
    esp += sizeof (int);
  }
  else {
    status = -1;
  }
  int i;struct thread *t = thread_current ();
  for (i = 2; i<128; i++)
  {
    if (t->files[i] != NULL){
      close_file (i);
    }
  }
  char *name = thread_current ()->name, *save;
  name = strtok_r (name, " ", &save);
  
  printf ("%s: exit(%d)\n", name, status);
  t->exitstatus = status;
  process_exit ();
  enum intr_level old_level = intr_disable ();
  sema_up (&t->almostexit);
  thread_block ();
  intr_set_level (old_level);
  thread_exit ();
}

static int
exec (void *esp)
{
  checkvalid(esp, sizeof (char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);
  checkvalidstring(file_name);
  lock_acquire (&file_lock);
  tid_t tid = process_execute (file_name);
  lock_release (&file_lock);
  struct thread *child = getChildThread(tid);
  if (child == NULL)
    return -1;
  
  sema_down (&child->loaded);
  if (!child->load)
    tid = -1;
  return tid;
}

static int
wait (void *esp)
{
  checkvalid(esp, sizeof (int));
  int pid = *((int *) esp);
  esp += sizeof (int);
  struct thread *child = getChildThread (pid);
  if (child == NULL) 
    return -1;
    
  sema_down (&child->almostexit);
  int status = child->exitstatus;
  list_remove(&child->parentelement);
  thread_unblock (child);
  return status;
}

static int
create (void *esp)
{
  checkvalid (esp, sizeof(char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);
  checkvalidstring (file_name);
  checkvalid (esp, sizeof(unsigned));
  unsigned initial_size = *((unsigned *) esp);
  esp += sizeof (unsigned);
  lock_acquire (&file_lock);
  int status = filesys_create (file_name, initial_size);
  lock_release (&file_lock);
  return status;
}

static int
remove (void *esp)
{
  checkvalid (esp, sizeof(char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);
  checkvalidstring (file_name);
  lock_acquire (&file_lock);
  int status = filesys_remove (file_name);
  lock_release (&file_lock);
  return status;
}

static int
open (void *esp)
{
  checkvalid (esp, sizeof(char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);
  checkvalidstring (file_name);
  lock_acquire (&file_lock);
  struct file *f = filesys_open (file_name);
  lock_release (&file_lock);
  if (f == NULL)
    return -1;
  struct thread *t = thread_current ();
  int i;
  for (i = 2; i<128; i++)
  {
    if (t->files[i] == NULL){
      t->files[i] = f;
      break;
    }
  }

  if (i == 128)
    return -1;
  else
    return i;
}

static int
filesize (void *esp)
{
  checkvalid (esp, sizeof(int));
  int fd = *((int *) esp);
  esp += sizeof (int);
  struct thread *t = thread_current ();
  if (checkfd (fd) && t->files[fd] != NULL)
  {  
    lock_acquire (&file_lock);
    int size = file_length (t->files[fd]);
    lock_release (&file_lock);
    return size;
  }
  return -1;
}

static int
read (void *esp)
{
  checkvalid (esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);
  checkvalid (esp, sizeof(void *));
  const void *buffer = *((void **) esp);
  esp += sizeof (void *);
  checkvalid (esp, sizeof(unsigned));
  unsigned size = *((unsigned *) esp);
  esp += sizeof (unsigned);
  checkvalid (buffer, size);
  struct thread *t = thread_current ();
  if (fd == STDIN_FILENO)
  {
    lock_acquire (&file_lock);
    int i;
    for (i = 0; i<size; i++)
      *((uint8_t *) buffer+i) = input_getc ();

    lock_release (&file_lock);
    return i;
  }
  else if (checkfd (fd) && fd >=2 && t->files[fd] != NULL)
  {
    lock_acquire (&file_lock);
    int read = file_read (t->files[fd], buffer, size);
    lock_release (&file_lock);
    return read;
  }
  return 0;
}

static int
write (void *esp)
{
  checkvalid (esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);
  checkvalid (esp, sizeof(void *));
  const void *buffer = *((void **) esp);
  esp += sizeof (void *);
  checkvalid (esp, sizeof(unsigned));
  unsigned size = *((unsigned *) esp);
  esp += sizeof (unsigned);
  checkvalid (buffer, size);
  struct thread *t = thread_current ();
  if (fd == STDOUT_FILENO)
  {
    lock_acquire (&file_lock);
    int i;
    for (i = 0; i<size; i++)
      putchar (*((char *) buffer + i));
    lock_release (&file_lock);
    return i;
  }
  else if (checkfd(fd) && fd >=2 && t->files[fd] != NULL)
  {
    lock_acquire (&file_lock);
    int written = file_write (t->files[fd], buffer, size);
    lock_release (&file_lock);
    return written;
  }
  return 0;
}


static int
seek (void *esp)
{
  checkvalid (esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  checkvalid (esp, sizeof(unsigned));
  unsigned position = *((unsigned *) esp);
  esp += sizeof (unsigned);

  struct thread *t = thread_current ();

  if (checkfd (fd) && t->files[fd] != NULL)
  {
    lock_acquire (&file_lock);
    file_seek (t->files[fd], position);
    lock_release (&file_lock);
  }
}

static int
tell (void *esp)
{
  checkvalid (esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  struct thread *t = thread_current ();

  if (checkfd (fd) && t->files[fd] != NULL)
  {
    lock_acquire (&file_lock);
    int position = file_tell (t->files[fd]);
    lock_release (&file_lock);
    return position;
  }
  return -1;
}

static int
close (void *esp)
{
  checkvalid(esp, sizeof(int));
  int fd = *((int *) esp);
  esp += sizeof (int);
  if (checkfd(fd))
  close_file (fd);
}

static int
mmap (void *esp)
{
  thread_exit ();
}

static int
munmap (void *esp)
{
  thread_exit ();
}

static int
chdir (void *esp)
{
  thread_exit ();
}

static int
mkdir (void *esp)
{
  thread_exit ();
}

static int
readdir (void *esp)
{
  thread_exit ();
}

static int
isdir (void *esp)
{
  thread_exit ();
}

static int
inumber (void *esp)
{
  thread_exit ();
}

static int (*syscalls []) (void *) =
  {
    halt,
    exit,
    exec,
    wait,
    create,
    remove,
    open,
    filesize,
    read,
    write,
    seek,
    tell,
    close,
    mmap,
    munmap,
    chdir,
    mkdir,
    readdir,
    isdir,
    inumber
  };

const int num_calls = sizeof (syscalls) / sizeof (syscalls[0]);
static void
checkvalid (const void *ptr,size_t size)
{
  uint32_t *pd = thread_current ()->pagedir;
  if ( ptr == NULL || !is_user_vaddr (ptr) || pagedir_get_page (pd, ptr) == NULL)
  {
    exit (NULL);
  }
  if(size!=1)
  {
     const void *ptr2=ptr+size-1;
     if ( ptr2 == NULL || !is_user_vaddr (ptr2) || 		  pagedir_get_page (pd, ptr2) == NULL)
     {
         exit (NULL);
     }
  }
}

static void
checkvalidstring(const char *s)
{
  checkvalid (s, sizeof(char));
  while (*s != '\0')
    checkvalid (s++, sizeof(char));
}

void
syscall_init (void) 
{
  lock_init (&file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  void *esp = f->esp;

  checkvalid (esp,sizeof(int));
  int syscall_num = *((int *) esp);
  esp += sizeof(int);

  //printf("\nSys: %d", syscall_num);
  checkvalid (esp,sizeof(void *));
  if (syscall_num >= 0 && syscall_num < num_calls)
  {
    int (*function) (void *) = syscalls[syscall_num];
    int ret = function (esp);
    f->eax = ret;
  }
  else
  {
    /* TODO:: Raise Exception */
    printf ("\nError, invalid syscall number.");
    thread_exit ();
  }
}

static void
close_file (int fd)
{
  struct thread *t = thread_current ();
  if (t->files[fd] != NULL)
  {
    lock_acquire (&file_lock);
    file_close (t->files[fd]);
    t->files[fd] = NULL;
    lock_release (&file_lock);
  }
}

static int
checkfd (int fd)
{
  return fd >= 0 && fd < 128; 
}
