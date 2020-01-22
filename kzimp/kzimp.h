/* Credits:
 *  -The basic lines come from Linux Device Drivers 3rd edition
 *  -some code from kl-shcom (linux_kernel_local_multicast) and zimp (in user-space)
 *
 *  Developed for an x86-64: we allocate all the memory in ZONE_NORMAL.
 *  On a x86 machine, ZONE_NORMAL is up to 896MB. On a x86-64 machine, ZONE_NORMAL
 *  represents the whole memory.
 */

#ifndef _KZIMP_MODULE_
#define _KZIMP_MODULE_

#include <linux/module.h>       /* Needed by all modules */
#include <linux/moduleparam.h>  /* This module takes arguments */
#include <linux/fs.h>           /* (un)register the block device - file operations */
#include <linux/cdev.h>         /* char device */
#include <linux/spinlock.h>     /* spinlock */
#include <linux/wait.h>         /* wait queues */
#include <linux/list.h>         /* linked list */
#include <linux/poll.h>         /* poll_table structure */

#define DRIVER_AUTHOR "Pierre Louis Aublin <pierre-louis.aublin@inria.fr>"
#define DRIVER_DESC   "Kernel module of the ZIMP communication mechanism"

// define it if you want only 1 reader to wake up the writers
// Provides the same results whether it is present or not.
//#define ATOMIC_WAKE_UP

// define CHANNEL_WRITE_IDX_ATOMIC if you want next_write_idx to be an atomic_t
// which will overflow. The writers will compute the modulo without a lock.
//#define CHANNEL_WRITE_IDX_ATOMIC

// even if all the machines do not necessarily have lines of 64B, we don't really care
#define CACHE_LINE_SIZE 64

// Used to define the size of the pad member
// The last modulo is to prevent the padding to add CACHE_LINE_SIZE bytes to the structure
#define PADDING_SIZE(S) ((CACHE_LINE_SIZE - ((S) % CACHE_LINE_SIZE)) % CACHE_LINE_SIZE)

// This module takes the following arguments:
static int nb_max_communication_channels = 4;
module_param(nb_max_communication_channels, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(nb_max_communication_channels, " The max number of communication channels.");

static int default_channel_size = 10;
module_param(default_channel_size, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(default_channel_size, " The default size of the new channels.");

static int default_max_msg_size = 1024;
module_param(default_max_msg_size, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(default_max_msg_size, " The default max size of the new channels messages.");

static long default_timeout_in_ms = 5000;
module_param(default_timeout_in_ms, long, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(default_timeout_in_ms, " The default timeout (in miliseconds) of the new channels.");

// file /dev/<DEVICE_NAME>
#define DEVICE_NAME "kzimp"

/* file /proc/<procfs_name> */
#define procfs_name "kzimp"

// FILE OPERATIONS
static int kzimp_open(struct inode *, struct file *);
static int kzimp_release(struct inode *, struct file *);
static ssize_t kzimp_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t kzimp_write(struct file *, const char __user *, size_t, loff_t *);
static unsigned int kzimp_poll(struct file *filp, poll_table *wait);

// an open file is associated with a set of functions
static struct file_operations kzimp_fops =
{
    .owner = THIS_MODULE,
    .open = kzimp_open,
    .release = kzimp_release,
    .read = kzimp_read,
    .write = kzimp_write,
    .poll = kzimp_poll,
};

#define KZIMP_HEADER_SIZE (sizeof(unsigned long)+sizeof(int)+sizeof(short))

// what is a message
// it must be packed so that we can compute the checksum
struct kzimp_message
{
  unsigned long bitmap; /* the bitmap, alone on  */
  int len;              /* length of the message */

  char *data;           /* the message content */
#ifdef ATOMIC_WAKE_UP
  atomic_t waking_up_writer;  /* is there someone waking up the writers? */
#endif

  // padding (to avoid false sharing)
#ifdef ATOMIC_WAKE_UP
  char __p2[PADDING_SIZE(KZIMP_HEADER_SIZE + sizeof(short) + sizeof(char*) + sizeof(atomic_t))];
#else
  char __p2[PADDING_SIZE(KZIMP_HEADER_SIZE + sizeof(short) + sizeof(char*))];
#endif
}__attribute__((__packed__, __aligned__(CACHE_LINE_SIZE)));

// kzimp communication channel
struct kzimp_comm_chan
{
  int channel_size;                 /* max number of messages in the channel */
  int compute_checksum;             /* do we compute the checksum? 0: no, 1: yes, 2: partial */
  unsigned long multicast_mask;     /* the multicast mask, used for the bitmap */
  struct kzimp_message* msgs;       /* the messages of the channel */
  char *messages_area;              /* pointer to the big allocated area of messages */
  long timeout_in_ms;               /* writer's timeout in miliseconds */
  wait_queue_head_t rq, wq;         /* the wait queues */

  // these variables are used by the writers only.
#ifdef CHANNEL_WRITE_IDX_ATOMIC
  atomic_t next_write_idx;
#else
  int next_write_idx;               /* position of the next written message */
#endif
  spinlock_t bcl;                   /* the Big Channel Lock :) */

  int max_msg_size;                 /* max message size */
  int nb_readers;                   /* number of readers */
  struct list_head readers;         /* List of pointers to the readers' control structure */
  int chan_id;                      /* id of this channel */
  struct cdev cdev;                 /* char device structure */
}__attribute__((__aligned__(CACHE_LINE_SIZE)));

// Each process that uses the channel to read has a control structure.
struct kzimp_ctrl
{
  int next_read_idx;               /* index of the next read in the channel */
  int bitmap_bit;                  /* position of the bit in the multicast mask modified by this reader */
  pid_t pid;                       /* pid of this reader */
  int online;                      /* is this reader still active or not? */
  struct list_head next;           /* pointer to the next reader on this channel */
  struct kzimp_comm_chan *channel; /* pointer to the channel */
}__attribute__((__aligned__(CACHE_LINE_SIZE)));

// return 1 if the writer can writeits message, 0 otherwise
static inline int writer_can_write(unsigned long bitmap)
{
  return (bitmap == 0);
}

// return 1 if the reader has a message to read, 0 otherwise
static inline int reader_can_read(unsigned long bitmap, int bit)
{
  return (bitmap & (1 << bit));
}


MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);

#endif
