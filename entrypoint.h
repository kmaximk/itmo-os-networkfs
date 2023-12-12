#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "http.h"

#define MAX_BYTES 512

struct dentry *networkfs_lookup(struct inode *parent, struct dentry *child,
                                unsigned int flag);

struct inode *networkfs_get_inode(struct super_block *sb,
                                  const struct inode *parent, umode_t mode,
                                  int i_ino);

char *escape_name(const char *name, size_t size);

int remove_http_call(struct inode *parent, struct dentry *child, char *type);

int create_http_call(struct dentry *child, struct inode *parent, umode_t mode,
                     int type);

int networkfs_save_buffer(struct file *filp);

struct entry_info {
  unsigned char entry_type;  // DT_DIR (4) or DT_REG (8)
  ino_t ino;
};

struct entries {
  size_t entries_count;
  struct entry {
    unsigned char entry_type;  // DT_DIR (4) or DT_REG (8)
    ino_t ino;
    char name[256];
  } entries[16];
};

struct content {
  __u64 content_length;
  char content[MAX_BYTES];
};