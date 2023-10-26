#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

#include <linux/slab.h>
#include <linux/uaccess.h>  
#include <linux/kernel.h>   
#include <linux/module.h>   
#include <linux/fs.h>       
#include <linux/string.h>   
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/ioctl.h>

//Our custom definitions of IOCTL operations
#include "message_slot.h" 

MODULE_LICENSE("GPL");

// strcuts
// channels are a linked list

typedef struct channel {
    unsigned long id;
    struct  channel* next;
    int msg_len;
    char msg[MAX_MSG_LEN];
} channel;

typedef struct slot {
    int minor;
    struct channel* head;
} slot;

// a struct for file-> private_data
// contains minor aka the slot this file is linked to and channel id aka the channel this file is linked to 

typedef struct file_info
{
  int minor;
  unsigned long id;
} fd_info;

// auxilary functions declarations
static channel * find_channel(fd_info * info);

static slot* slots[MAX_SLOTS+1] = {NULL};

// helper func to find the desired channel by the curr minor and ths channel_id
// used by device_read and device_write

static channel * find_channel(fd_info * info){
  channel * curr_channel;
  channel * prev_channel;
  slot * curr_slot;

  if (info == NULL){
    return NULL;
  }
  curr_slot = slots[info->minor];
  curr_channel = curr_slot->head;

  while (curr_channel != NULL){
      // channel is found
      if (curr_channel->id == info->id){
        return curr_channel;
      }
      prev_channel = curr_channel;
      curr_channel = curr_channel->next;
  }
  curr_channel = (channel*)kmalloc(sizeof(channel), GFP_KERNEL);
  if (curr_channel == NULL){
    return NULL;
  }
  curr_channel->id = info->id;
  curr_channel->msg_len = 0;
  curr_channel->next = NULL;
  if (curr_slot->head != NULL){
    prev_channel->next = curr_channel;
  }
  else{
    curr_slot->head = curr_channel;
  }
    
  return curr_channel;
}


//================== DEVICE FUNCTIONS ===========================
static int device_open( struct inode* inode,
                        struct file*  file )
{
  slot *curr_slot = NULL;
  fd_info * info = NULL;
  int curr_minor = iminor(inode);
  
  // slot doesn't exist
  if (slots[curr_minor] == NULL){
    curr_slot = (slot*)kmalloc(sizeof(slot), GFP_KERNEL);
    if (curr_slot == NULL){
      return -ENOMEM;
    }
    curr_slot->minor = curr_minor;
    curr_slot->head = NULL;
    slots[curr_minor] = curr_slot;
  }
  info = (fd_info*)kmalloc(sizeof(fd_info), GFP_KERNEL);
  if (info == NULL){
      return -ENOMEM;
    }
  info->minor = curr_minor;
  info->id = 0;    // channel_id for file will known only in ioctl
  file->private_data = (void *) info;

  return SUCCESS;
}

//---------------------------------------------------------------
// a process which has already opened
// the device file attempts to read from it
static ssize_t device_read( struct file* file,
                            char __user* buffer,
                            size_t       length,
                            loff_t*      offset )
{
  int i;
  int check;
  channel * curr_channel = NULL;  
  fd_info * info = (fd_info*)(file->private_data);

  if (buffer == NULL){
    return -EINVAL;
  }
  // explanation for acsses_ok use here
  //https://stackoverflow.com/questions/6887057/linux-kernel-userspace-buffers-do-access-ok-and-wait-create-a-race-condition
  if (!access_ok(buffer, length)){
    return -EINVAL;
  }
  // check if a channel has been set on the fd
  if (info == NULL || info->id == 0){
    return -EINVAL;
  }
  curr_channel = find_channel(info);
  if (curr_channel == NULL){
    return -EINVAL;
  }
  if (curr_channel->msg_len == 0){
    return -EWOULDBLOCK;
  }
  if (length < (curr_channel->msg_len)){
    return -ENOSPC;
  }

  for (i = 0; i < curr_channel->msg_len; i++){
    check =  put_user((curr_channel->msg)[i], &buffer[i]);
    if (check != 0){
      return -EINVAL;
    }
  }
  return i;
}

//---------------------------------------------------------------
// a processs which has already opened
// the device file attempts to write to it
static ssize_t device_write( struct file*       file,
                             const char __user* buffer,
                             size_t             length,
                             loff_t*            offset)
{
  int i;
  int check;
  channel * curr_channel = NULL;
  fd_info * info = (fd_info*)(file->private_data);
  
  if (buffer == NULL){
    return -EINVAL;
  }
  // explanation for acsses_ok use here
  //https://stackoverflow.com/questions/6887057/linux-kernel-userspace-buffers-do-access-ok-and-wait-create-a-race-condition
  if (!access_ok(buffer, length)){
    return -EINVAL;
  }
   // validate msg len
  if (length ==0 || length > MAX_MSG_LEN){
    return -EMSGSIZE;
  }
  // check if a channel has been set on the fd
  if (info == NULL || info->id == 0){
    return -EINVAL;
  }
  curr_channel = find_channel(info);
  if (curr_channel == NULL){
    return -EINVAL;
  }
  if (curr_channel->msg_len != 0){
    memset(curr_channel->msg, 0, MAX_MSG_LEN);
  }
  for (i = 0; i < length; i++){
    check = get_user((curr_channel->msg)[i], &buffer[i]);
    if (check != 0){
      return -EINVAL;
    }
  }
  curr_channel->msg_len = length;

  return length;
}

//----------------------------------------------------------------
static long device_ioctl( struct   file* file,
                          unsigned int   ioctl_command_id,
                          unsigned long  ioctl_param )
{
  fd_info * info = NULL;
  // can change this with validity ARG
  if ((ioctl_command_id != MSG_SLOT_CHANNEL) || (ioctl_param == 0)){
    return -EINVAL;
  }
  info = (fd_info*)file->private_data;
  if (info == NULL){
    return -EINVAL;
  }
  // associate channel id with the fd
  info->id = ioctl_param;

  return SUCCESS;
}

//----------------------------------------------------------------
static int device_release( struct inode* inode,
                           struct file*  file)
{
  fd_info * info;
  info = file->private_data;
  kfree(info);
  return SUCCESS;
}

//==================== DEVICE SETUP =============================

// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops = {
  .owner	  = THIS_MODULE, 
  .read           = device_read,
  .write          = device_write,
  .open           = device_open,
  .unlocked_ioctl = device_ioctl,
  .release = device_release,
};

//---------------------------------------------------------------
// Initialize the module - Register the character device
static int __init ms_init(void)
{
  int rc = -1;
 
  // Register driver capabilities. Obtain MAJOR_NUM num
  rc = register_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME, &Fops);

  // Negative values signify an error
  if( rc < 0 ) {
    printk(KERN_ERR "%s registraion failed for  %d\n", DEVICE_FILE_NAME, MAJOR_NUM);
    return rc;
  }

  return SUCCESS;
}

//---------------------------------------------------------------
static void __exit ms_cleanup(void)
{
  int i;
  slot * curr_slot;
  channel * curr_channel;
  channel * tmp_channel;

  for (i = 0; i < MAX_SLOTS + 1; i++){
    curr_slot = slots[i];
    if (curr_slot == NULL){
      continue;
    }
    curr_channel = curr_slot->head;
    while (curr_channel != NULL){
      tmp_channel = curr_channel->next;
      kfree(curr_channel);
      curr_channel = tmp_channel;
    }
    kfree(curr_slot);
  }
  // Unregister the device
  unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);
}

//---------------------------------------------------------------
module_init(ms_init);
module_exit(ms_cleanup);

//========================= END OF FILE =========================