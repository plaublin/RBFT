/* kzimp - character device module */

#include <linux/kernel.h>      /* Needed for KERN_INFO */
#include <linux/init.h>        /* Needed for the macros */
#include <linux/proc_fs.h>     /* proc fs */
#include <asm/uaccess.h>       /* copy_from_user function */
#include <linux/sched.h>       /* current macro */
#include <linux/fcntl.h>       /* some flags */
#include <asm/bitops.h>        /* atomic bitwise ops */
#include <asm/param.h>         /* HZ value */
#include <linux/sched.h>       /* TASK_*INTERRUPTIBLE macros */
#include <linux/slab.h>         /* kmalloc */
#include <linux/vmalloc.h>      /* vmalloc */

#include "kzimp.h"

// debug flag
#undef DEBUG

// character device files need a major and minor number.
// They are set automatically when loading the module
static int kzimp_major, kzimp_minor;

// holds device information
static dev_t kzimp_dev_t;

// pointer to the /proc file
static struct proc_dir_entry *proc_file;

// array of communication channels
static struct kzimp_comm_chan *kzimp_channels;


// return the bit to modify in the multicast mask for this reader
// given the communication channel chan or -1 if an error has occured
static int get_new_bitmap_bit(struct kzimp_comm_chan *chan)
{
  int bit_pos, nr_bits;

  nr_bits = sizeof(chan->multicast_mask) * 8; // sizeof() returns a number of bytes. We want bits.
  bit_pos = find_first_zero_bit(&chan->multicast_mask, nr_bits);

  if (bit_pos != nr_bits)
  {
    set_bit(bit_pos, &chan->multicast_mask);
  }
  else
  {
    bit_pos = -1;
  }

  return bit_pos;
}

/*
 * kzimp open operation.
 * Returns:
 *  . -ENOMEM if the memory allocations fail
 *  . -1 if the maximum number of readers have been reached
 *  . 0 otherwise
 */
static int kzimp_open(struct inode *inode, struct file *filp)
{
  struct kzimp_comm_chan *chan; /* channel information */
  struct kzimp_ctrl *ctrl;

  chan = container_of(inode->i_cdev, struct kzimp_comm_chan, cdev);

  ctrl = kmalloc(sizeof(*ctrl), GFP_KERNEL);
  if (unlikely(!ctrl))
  {
    printk(KERN_ERR "kzimp: kzimp_ctrl allocation error\n");
    return -ENOMEM;
  }

  ctrl->pid = current->pid;
  ctrl->channel = chan;

  if (filp->f_mode & FMODE_READ)
  {
    spin_lock(&chan->bcl);

    // we set next_read_idx to the next position where the writer is going to write
    // so that it gets the next message
#ifdef CHANNEL_WRITE_IDX_ATOMIC
    ctrl->next_read_idx = atomic_read(&chan->next_write_idx);
#else
    ctrl->next_read_idx = chan->next_write_idx;
#endif
    ctrl->bitmap_bit = get_new_bitmap_bit(chan);
    ctrl->online = 1;

    chan->nb_readers++;
    list_add_tail(&ctrl->next, &chan->readers);

    spin_unlock(&chan->bcl);

    if (ctrl->bitmap_bit == -1)
    {
      printk(KERN_ERR "Maximum number of readers on the channel %i has been reached: %i\n", chan->chan_id, chan->nb_readers);
      return -1;
    }
  }
  else
  {
    ctrl->next_read_idx = -1;
    ctrl->bitmap_bit = -1;
    ctrl->online = -1;
    ctrl->next.prev = ctrl->next.next = NULL;
  }

  filp->private_data = ctrl;

  return 0;
}

/*
 * kzimp release operation.
 * Returns:
 *  . 0: it always succeeds
 */
static int kzimp_release(struct inode *inode, struct file *filp)
{
  int i;

  struct kzimp_comm_chan *chan; /* channel information */
  struct kzimp_ctrl *ctrl;

  ctrl = filp->private_data;

  if (filp->f_mode & FMODE_READ)
  {
    chan = ctrl->channel;

    spin_lock(&chan->bcl);

    clear_bit(ctrl->bitmap_bit, &(chan->multicast_mask));

    list_del(&ctrl->next);
    chan->nb_readers--;

    spin_unlock(&chan->bcl);

    for (i = 0; i < chan->channel_size; i++)
    {
      clear_bit(ctrl->bitmap_bit, &(chan->msgs[i].bitmap));
    }
  }

  kfree(ctrl);

  return 0;
}

/*
 * kzimp wait for reading
 * Blocking by default. May be non blocking (if O_NONBLOCK is set when calling open()).
 * Returns:
 *  . -EAGAIN if the operations are non-blocking and the call would block.
 *  . -EINTR if the process has been interrupted by a signal while waiting
 *  . 0 otherwise
 */
static ssize_t kzimp_wait_for_reading_if_needed(struct file *filp,
    struct kzimp_message *m)
{
  DEFINE_WAIT(__wait);

  ssize_t ret = 0;
  struct kzimp_comm_chan *chan; /* channel information */
  struct kzimp_ctrl *ctrl;

  ctrl = filp->private_data;
  chan = ctrl->channel;

  // we do not need this test to be atomic
  while (!reader_can_read(m->bitmap, ctrl->bitmap_bit))
  {
    prepare_to_wait(&chan->rq, &__wait, TASK_INTERRUPTIBLE);

    // file is open in no-blocking mode
    if (filp->f_flags & O_NONBLOCK)
    {
      //printk(KERN_WARNING "kzimp: process %i in read returns because of non-blocking ops\n", current->pid);
      ret = -EAGAIN;
      break;
    }

    if (unlikely(signal_pending(current)))
    {
      printk(KERN_WARNING "kzimp: process %i in read has been interrupted\n", current->pid);
      ret = -EINTR;
      break;
    }

    // A simple schedule() causes a race condition.
    // If we check again the condition then there is no problem.
    // With the schedule_timeout() performance seem to be better.
    // if (!reader_can_read(m->bitmap, ctrl->bitmap_bit))
    //   schedule();
    schedule_timeout(HZ / 100);
  }
  finish_wait(&chan->rq, &__wait);

  return ret;
}


/*
 * finalize the write: unset the bit in the bitmap, wake up the writers, update next_write_idx
 */
static int finalize_read(struct kzimp_message *m, struct kzimp_ctrl *ctrl,
    struct kzimp_comm_chan *chan, size_t count)
{
  int retval;

  retval = count;

  // the timeout at the writer may have expired, and the writer may have started to write
  // a new message at m
  if (likely(ctrl->online))
  {
    clear_bit(ctrl->bitmap_bit, &m->bitmap);
    if (writer_can_write(m->bitmap)
#ifdef ATOMIC_WAKE_UP
        && !atomic_cmpxchg(&m->waking_up_writer, 0, 1)
#endif
    )
    {
      wake_up_interruptible(&chan->wq);

#ifdef ATOMIC_WAKE_UP
      atomic_set(&m->waking_up_writer, 0);
#endif
    }

    ctrl->next_read_idx = (ctrl->next_read_idx + 1) % chan->channel_size;
  }
  else
  {
    printk(KERN_WARNING "kzimp: Process %i in read is no longer active\n", current->pid);
    retval = -EBADF;
  }

  return retval;
}

/*
 * kzimp read operation.
 * Blocking by default. May be non blocking (if O_NONBLOCK is set when calling open()).
 * Returns:
 *  . -EFAULT if the copy to buf has failed
 *  . -EAGAIN if the operations are non-blocking and the call would block.
 *  . -EBADF if this reader is no longer online (because the writer has experienced a timeout)
 *  . -EINTR if the process has been interrupted by a signal while waiting
 *  . 0 if there has been an error when reading (count is <= 0)
 *  . The number of read bytes otherwise
 */
static ssize_t kzimp_read
(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
  int retval;
  struct kzimp_message *m;
  DEFINE_WAIT(__wait);

  struct kzimp_comm_chan *chan; /* channel information */
  struct kzimp_ctrl *ctrl;

  ctrl = filp->private_data;
  chan = ctrl->channel;

  m = &(chan->msgs[ctrl->next_read_idx]);

  retval = kzimp_wait_for_reading_if_needed(filp, m);
  if (retval)
  {
    return retval;
  }

  // check length
  count = (m->len < count ? m->len : count);

  if (unlikely(copy_to_user(buf, m->data, count)))
  {
    printk(KERN_ERR "kzimp: copy_to_user failed for process %i in read\n", current->pid);
    return -EFAULT;
  }

  retval = finalize_read(m, ctrl, chan, count);

  return retval;
}

// When the timeout expires, the writer removes the bits that are at 1 in this bitmap, for all the messages
// chan is the channel, m is the struct kzimp_message where to write the current message.
static void handle_timeout(struct kzimp_comm_chan *chan,
    struct kzimp_message *m)
{
  int i;
  struct list_head *p;
  struct kzimp_ctrl *ptr;
  unsigned long tmp;

  unsigned long bitmap = m->bitmap;

  spin_lock(&chan->bcl);

  // remove the bits from the multicast mask
  tmp = chan->multicast_mask & ~bitmap;
  __asm volatile ("" : : : "memory");
  chan->multicast_mask = tmp; // this operation is atomic

  // remove the bits from all the messages
  for (i = 0; i < chan->channel_size; i++)
  {
    // test if bitmap is different from 0, otherwise we may loose a message:
    // process A                        process B
    //                      msg.bitmap = 0
    //   | a = msg.bitmap & ~bitmap        |
    //   |                                 | msg.bitmap = multicast_mask
    //   | msg.bitmap = a                  |
    // The bitmap is now 0 instead of multicast_mask. The message has been lost.
    if (chan->msgs[i].bitmap != 0)
    {
      chan->msgs[i].bitmap &= ~bitmap;
    }
  }

  // set the remaining readers on that bitmap to offline
  list_for_each(p, &chan->readers)
  {
    ptr = list_entry(p, struct kzimp_ctrl, next);
    if (reader_can_read(bitmap, ptr->bitmap_bit))
    {
      ptr->online = 0;
      printk(KERN_DEBUG "kzimp: Process %i in write. Process %i is offline\n", current->pid, ptr->pid);
    }
  }

  spin_unlock(&chan->bcl);
}

#ifdef CHANNEL_WRITE_IDX_ATOMIC
static inline int kzimp_modulo(int a, int b)
{
  int c;
  c = a % b;
  return (c < 0) ? c + b : c;
}
#endif

// Wait for writing if needed.
// Return 1 if everything is ok, an error otherwise.
static ssize_t kzimp_wait_for_writing_if_needed(struct file *filp,
    size_t count, struct kzimp_message **mf)
{
  ssize_t ret = 1;
  long to_expired;
  struct kzimp_message *m;
  DEFINE_WAIT(__wait);

#ifdef CHANNEL_WRITE_IDX_ATOMIC
  int next_wr_idx;
#endif

  struct kzimp_comm_chan *chan; /* channel information */
  struct kzimp_ctrl *ctrl;

  ctrl = filp->private_data;
  chan = ctrl->channel;

  // Check the validity of the arguments
  if (unlikely(count <= 0 || count > chan->max_msg_size))
  {
    printk(KERN_ERR "kzimp: count is not valid: %lu (process %i in write on channel %i)\n", (unsigned long)count, current->pid, chan->chan_id);
    return 0;
  }

#ifdef CHANNEL_WRITE_IDX_ATOMIC
  next_wr_idx = atomic_inc_return(&chan->next_write_idx);
  next_wr_idx = kzimp_modulo(next_wr_idx-1, chan->channel_size);
  m = &(chan->msgs[next_wr_idx]);
#else
  spin_lock(&chan->bcl);
  m = &(chan->msgs[chan->next_write_idx]);
  chan->next_write_idx = (chan->next_write_idx + 1) % chan->channel_size;
  spin_unlock(&chan->bcl);
#endif

  to_expired = 1;
  while (!writer_can_write(m->bitmap) && to_expired)
  {
    prepare_to_wait(&chan->wq, &__wait, TASK_INTERRUPTIBLE);

    // file is open in no-blocking mode
    if (filp->f_flags & O_NONBLOCK)
    {
      ret = -EAGAIN;
      break;
    }

    if (unlikely(signal_pending(current)))
    {
      printk(KERN_WARNING "kzimp: process %i in write has been interrupted\n", current->pid);
      ret = -EINTR;
      break;
    }

    to_expired = schedule_timeout(chan->timeout_in_ms * HZ / 1000);
  }
  finish_wait(&chan->wq, &__wait);

  if (unlikely(!to_expired))
  {
    printk(KERN_DEBUG "kzimp: timer has expired for process %i in write\n", current->pid);
    handle_timeout(chan, m);
  }

  *mf = m;

  return ret;
}

// finalize the write
static void kzimp_finalize_write(struct kzimp_comm_chan *chan,
    struct kzimp_message *m, size_t count)
{
  m->len = count;

  m->bitmap = chan->multicast_mask;

  // wake up sleeping readers
  wake_up_interruptible(&chan->rq);
}

/*
 * kzimp write operation.
 * Blocking call.
 * Sleeps until it can write the message.
 * FIXME: if there are more writers than the number of messages in the channel, there can be
 * FIXME: 2 writers on the same message. We assume the channel size is less than the number of writers.
 * FIXME: To fix that we can add a counter.
 * Returns:
 *  . 0 if the size of the user-level buffer is less or equal than 0 or greater than the maximal message size
 *  . -EFAULT if the buffer *buf is not valid
 *  . -EINTR if the process has been interrupted by a signal while waiting
 *  . -EAGAIN if the operations are non-blocking and the call would block.
 *  . The number of written bytes otherwise
 */
static ssize_t kzimp_write
(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
  struct kzimp_message *m;
  ssize_t ret;

  struct kzimp_comm_chan *chan; /* channel information */
  struct kzimp_ctrl *ctrl;

  ctrl = filp->private_data;
  chan = ctrl->channel;

  ret = kzimp_wait_for_writing_if_needed(filp, count, &m);
  if (unlikely(ret != 1))
  {
    return ret;
  }

  // copy_from_user returns the number of bytes left to copy
  if (unlikely(copy_from_user(m->data, buf, count)))
  {
    printk(KERN_ERR "kzimp: copy_from_user failed for process %i in write\n", current->pid);
    return -EFAULT;
  }

  kzimp_finalize_write(chan, m, count);

  return count;
}

// Called by select(), poll() and epoll() syscalls.
// pre-condition: must be called by a reader. The call does not work
// (and does not have sense) for a writer.
static unsigned int kzimp_poll(struct file *filp, poll_table *wait)
{
  unsigned int mask = 0;
  struct kzimp_message *m;

  struct kzimp_comm_chan *chan; /* channel information */
  struct kzimp_ctrl *ctrl;

  ctrl = filp->private_data;
  chan = ctrl->channel;

  poll_wait(filp, &chan->rq, wait);

  m = &(chan->msgs[ctrl->next_read_idx]);
  if (reader_can_read(m->bitmap, ctrl->bitmap_bit))
  {
    mask |= POLLIN | POLLRDNORM;
  }
  if (!ctrl->online)
  {
    mask |= POLLHUP; // kind of end-of-file
  }

  return mask;
}

static int kzimp_init_channel(struct kzimp_comm_chan *channel, int chan_id,
    int max_msg_size, int channel_size, long to,
    int init_lock)
{
  int i;
  char *addr;
  unsigned long size;

  channel->chan_id = chan_id;
  channel->max_msg_size = max_msg_size;
  channel->channel_size = channel_size;
  channel->timeout_in_ms = to;
  channel->multicast_mask = 0;
  channel->nb_readers = 0;
#ifdef CHANNEL_WRITE_IDX_ATOMIC
  atomic_set(&channel->next_write_idx, 0);
#else
  channel->next_write_idx = 0;
#endif
  init_waitqueue_head(&channel->rq);
  init_waitqueue_head(&channel->wq);
  INIT_LIST_HEAD(&channel->readers);

  size = (unsigned long) channel->max_msg_size
      * (unsigned long) channel->channel_size;
  channel->messages_area = vmalloc(size);
  if (unlikely(!channel->messages_area))
  {
    printk(KERN_ERR "kzimp: channel messages allocation of %lu bytes error\n", size);
    return -ENOMEM;
  }

  size = sizeof(*channel->msgs) * channel->channel_size;
  channel->msgs = kmalloc(size, GFP_KERNEL);
  if (unlikely(!channel->msgs))
  {
    printk(KERN_ERR "kzimp: channel messages allocation of %lu bytes error\n", size);
    return -ENOMEM;
  }

  addr = channel->messages_area;
  for (i = 0; i < channel->channel_size; i++)
  {
    channel->msgs[i].data = addr;
    channel->msgs[i].bitmap = 0;
#ifdef ATOMIC_WAKE_UP
    atomic_set(&channel->msgs[i].waking_up_writer, 0);
#endif
    addr += channel->max_msg_size;
  }

  if (init_lock)
  {
    spin_lock_init(&channel->bcl);
  }

  return 0;
}

// called when reading file /proc/<procfs_name>
static int kzimp_read_proc_file(char *page, char **start, off_t off, int count,
    int *eof, void *data)
{
  int i;
  int len;

  len = sprintf(page, "kzimp %s @ %s\n\n", __DATE__, __TIME__);
  len += sprintf(page + len, "nb_max_communication_channels = %i\n",
      nb_max_communication_channels);
  len += sprintf(page + len, "default_channel_size = %i\n",
      default_channel_size);
  len += sprintf(page + len, "default_max_msg_size = %i\n",
      default_max_msg_size);
  len += sprintf(page + len, "default_timeout_in_ms = %li\n",
      default_timeout_in_ms);

  len
  += sprintf(
      page + len,
      "chan_id\tchan_size\tmax_msg_size\tmulticast_mask\tnb_receivers\ttimeout_in_ms\n");
  for (i = 0; i < nb_max_communication_channels; i++)
  {
    len += sprintf(page + len, "%i\t%i\t%i\t%lx\t%i\t%li\n",
        kzimp_channels[i].chan_id, kzimp_channels[i].channel_size,
        kzimp_channels[i].max_msg_size, kzimp_channels[i].multicast_mask,
        kzimp_channels[i].nb_readers, kzimp_channels[i].timeout_in_ms);
  }

  return len;
}

static void kzimp_free_channel(struct kzimp_comm_chan *chan)
{
  vfree(chan->messages_area);
  kfree(chan->msgs);
}

// called when writing to file /proc/<procfs_name>
static int kzimp_write_proc_file(struct file *file, const char *buffer,
    unsigned long count, void *data)
{
  int err = 0;
  int len;
  int chan_id, max_msg_size, channel_size;
  long to;
  char* kbuff;

  len = count;

  kbuff = (char*) kmalloc(sizeof(char) * len, GFP_KERNEL);

  if (copy_from_user(kbuff, buffer, len))
  {
    kfree(kbuff);
    return -EFAULT;
  }

  kbuff[len - 1] = '\0';

  sscanf(kbuff, "%i %i %i %li", &chan_id, &channel_size, &max_msg_size, &to);

  kfree(kbuff);

  if (chan_id < 0 || chan_id >= nb_max_communication_channels)
  {
    // channel id not valid
    printk(KERN_WARNING "kzimp: channel id not valid: %i <= %i < %i", 0, chan_id, nb_max_communication_channels);
    return len;
  }

  if (channel_size < 0)
  {
    // channel size not valid
    printk(KERN_WARNING "kzimp: channel size not valid: %i < %i", channel_size, 0);
    return len;
  }

  if (max_msg_size <= 0)
  {
    // max message size not valid
    printk(KERN_WARNING "kzimp: max message size not valid: %i <= %i", max_msg_size, 0);
    return len;
  }

  if (to <= 0)
  {
    // timeout not valid
    printk(KERN_WARNING "kzimp: timeout not valid: %li <= %i", to, 0);
    return len;
  }

  spin_lock(&kzimp_channels[chan_id].bcl);

  // we can modify the channel only if there are no readers on it
  if (kzimp_channels[chan_id].nb_readers == 0)
  {
    kzimp_free_channel(&kzimp_channels[chan_id]);
    err = kzimp_init_channel(&kzimp_channels[chan_id], chan_id, max_msg_size,
        channel_size, to, 0);
  }

  spin_unlock(&kzimp_channels[chan_id].bcl);

  if (unlikely(err))
  {
    printk  (KERN_WARNING "kzimp: Error %i at initialization of channel %i", err, chan_id);
  }

  return len;
}

static int kzimp_init_cdev(struct kzimp_comm_chan *channel, int i)
{
  int err, devno;

  err = kzimp_init_channel(channel, i, default_max_msg_size,
      default_channel_size, default_timeout_in_ms, 1);
  if (unlikely(err))
  {
    printk(KERN_ERR "kzimp: Error %i at initialization of channel %i", err, i);
    return -1;
  }

  devno = MKDEV(kzimp_major, kzimp_minor + i);

  cdev_init(&channel->cdev, &kzimp_fops);
  channel->cdev.owner = THIS_MODULE;

  err = cdev_add(&channel->cdev, devno, 1);
  /* Fail gracefully if need be */
  if (unlikely(err))
  {
    printk(KERN_ERR "kzimp: Error %d adding kzimp%d", err, i);
    return -1;
  }

  return 0;
}

static void kzimp_del_cdev(struct kzimp_comm_chan *channel)
{
  cdev_del(&channel->cdev);
}

static int __init kzimp_start(void)
{
  int i;
  int result;

  // ADDING THE DEVICE FILES
  result = alloc_chrdev_region(&kzimp_dev_t, kzimp_minor, nb_max_communication_channels, DEVICE_NAME);
  kzimp_major = MAJOR(kzimp_dev_t);

  if (unlikely(result < 0))
  {
    printk(KERN_ERR "kzimp: can't get major %d\n", kzimp_major);
    return result;
  }

  kzimp_channels = kmalloc(nb_max_communication_channels * sizeof(struct kzimp_comm_chan), GFP_KERNEL);
  if (unlikely(!kzimp_channels))
  {
    printk(KERN_ERR "kzimp: channels allocation error\n");
    return -ENOMEM;
  }

  for (i=0; i<nb_max_communication_channels; i++)
  {
    result = kzimp_init_cdev(&kzimp_channels[i], i);
    if (unlikely(result))
    {
      printk (KERN_ERR "kzimp: creation of channel device %i failed\n", i);
      return -1;
    }
  }

  // CREATE /PROC FILE
  proc_file = create_proc_entry(procfs_name, 0444, NULL);
  if (unlikely(!proc_file))
  {
    remove_proc_entry(procfs_name, NULL);
    printk (KERN_ERR "kzimp: creation of /proc/%s file failed\n", procfs_name);
    return -1;
  }
  proc_file->read_proc = kzimp_read_proc_file;
  proc_file->write_proc = kzimp_write_proc_file;

  return 0;
}

// Note: we assume there are no processes still using the channels.
static void __exit kzimp_end(void)
{
  int i;

  // delete channels
  for (i=0; i<nb_max_communication_channels; i++)
  {
    kzimp_free_channel(&kzimp_channels[i]);
    kzimp_del_cdev(&kzimp_channels[i]);
  }
  kfree(kzimp_channels);

  // remove the /proc file
  remove_proc_entry(procfs_name, NULL);

  unregister_chrdev_region(kzimp_dev_t, nb_max_communication_channels);
}

module_init( kzimp_start);
module_exit( kzimp_end);
