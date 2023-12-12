#include "entrypoint.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ivanov Ivan");
MODULE_VERSION("0.01");

int networkfs_link(struct dentry *target, struct inode *parent,
                   struct dentry *child) {
  const char *name = child->d_name.name;
  struct inode *inode = d_inode(target);
  if (inode == NULL) {
    return -1;
  }
  char *escaped_name = escape_name(name, strlen(name));
  if (escaped_name == NULL) {
    return -ENOMEM;
  }
  char number2[8];
  sprintf(number2, "%lu", parent->i_ino);
  char number1[8];
  sprintf(number1, "%lu", inode->i_ino);
  int res =
      networkfs_http_call(parent->i_sb->s_fs_info, "link", NULL, 0, 3, "source",
                          number1, "parent", number2, "name", escaped_name);
  kfree(escaped_name);
  if (res != 0) {
    return -1;
  } else {
    inode->__i_nlink++;
    return 0;
  }
}

int networkfs_setattr(struct user_namespace *user_ns, struct dentry *entry,
                      struct iattr *attr) {
  int ret = setattr_prepare(user_ns, entry, attr);
  if (ret != 0) {
    return ret;
  }
  struct inode *inode = d_inode(entry);
  if (attr->ia_valid & ATTR_OPEN) {
    inode->i_size = attr->ia_size;
  }
  return 0;
}

int networkfs_open(struct inode *inode, struct file *filp) {
  struct content *response =
      (struct content *)kzalloc(sizeof(struct content), GFP_KERNEL);
  if (response == NULL) {
    return -ENOMEM;
  }
  char number[8];
  sprintf(number, "%lu", inode->i_ino);
  int res =
      networkfs_http_call(inode->i_sb->s_fs_info, "read", (char *)response,
                          sizeof(*response), 1, "inode", number);
  if (res != 0) {
    kfree(response);
    return 0;
  }
  filp->private_data =
      (void *)kzalloc(response->content_length + 1, GFP_KERNEL);
  if (filp->private_data == NULL) {
    kfree(response);
    return -ENOMEM;
  }
  memcpy(filp->private_data, response->content, response->content_length);
  inode->i_size = response->content_length;
  kfree(response);
  if (filp->f_flags & O_APPEND) {
    generic_file_llseek(filp, 0, SEEK_END);
  }
  return 0;
}

ssize_t networkfs_read(struct file *filp, char *buffer, size_t len,
                       loff_t *offset) {
  if (*offset >= filp->f_inode->i_size) {
    return 0;
  }
  if (len + *offset >= filp->f_inode->i_size) {
    len = filp->f_inode->i_size - *offset;
  }
  if (copy_to_user(buffer, filp->private_data + *offset, len) != 0) {
    return -EFAULT;
  }
  *offset += len;
  return len;
}

ssize_t networkfs_write(struct file *filp, const char *buffer, size_t len,
                        loff_t *offset) {
  if (*offset >= MAX_BYTES) {
    return -EDQUOT;
  } else if (*offset + len > MAX_BYTES) {
    len = MAX_BYTES - *offset;
  }
  void *temp = filp->private_data;
  filp->private_data =
      (char *)kzalloc(len + filp->f_inode->i_size + 1, GFP_KERNEL);
  if (filp->private_data == NULL) {
    return -ENOMEM;
  }
  memcpy(filp->private_data, temp, filp->f_inode->i_size);
  kfree(temp);
  if (copy_from_user(filp->private_data + *offset, buffer, len) != 0) {
    return -EFAULT;
  }
  filp->f_inode->i_size += len;
  *offset += len;
  return len;
}

int networkfs_save_buffer(struct file *filp) {
  if (filp == NULL || filp->f_inode->i_size == 0) {
    return 0;
  }
  char number[8];
  sprintf(number, "%lu", filp->f_inode->i_ino);
  char *escaped_name = escape_name(filp->private_data, filp->f_inode->i_size);
  if (escaped_name == NULL) {
    return -ENOMEM;
  }
  int res = networkfs_http_call(filp->f_inode->i_sb->s_fs_info, "write", NULL,
                                0, 2, "inode", number, "content", escaped_name);
  kfree(escaped_name);
  if (res != 0) {
    return -1;
  }
  return 0;
}

int networkfs_flush(struct file *filp, fl_owner_t id) {
  return networkfs_save_buffer(filp);
}

int networkfs_fsync(struct file *filp, loff_t begin, loff_t end, int datasync) {
  return networkfs_save_buffer(filp);
}

int networkfs_release(struct inode *inode, struct file *filp) {
  kfree(filp->private_data);
  return 0;
}

char *escape_name(const char *name, size_t size) {
  char *escaped_name = (char *)kzalloc(size * 3 + 1, GFP_KERNEL);
  if (escaped_name == NULL) {
    return NULL;
  }
  for (int i = 0; i < strlen(name); i++) {
    sprintf((char *)escaped_name + 3 * i, "%%%02X", name[i]);
  }
  return escaped_name;
}

int create_http_call(struct dentry *child, struct inode *parent, umode_t mode,
                     int type) {
  const char *name = child->d_name.name;
  char *escaped_name = escape_name(name, strlen(name));
  if (escaped_name == NULL) {
    return -ENOMEM;
  }
  ino_t ino = 0;
  char number[8];
  sprintf(number, "%lu", parent->i_ino);
  int res = networkfs_http_call(parent->i_sb->s_fs_info, "create", (char *)&ino,
                                sizeof(ino_t), 3, "parent", number, "name",
                                escaped_name, "type",
                                type == S_IFREG ? "file" : "directory");
  kfree(escaped_name);
  if (res == 0) {
    struct inode *inode =
        networkfs_get_inode(parent->i_sb, parent, mode | type, ino);
    d_add(child, inode);
    return 0;
  } else {
    return -1;
  }
}

int remove_http_call(struct inode *parent, struct dentry *child, char *type) {
  const char *name = child->d_name.name;
  char number[8];
  sprintf(number, "%lu", parent->i_ino);
  char *escaped_name = escape_name(name, strlen(name));
  if (escaped_name == NULL) {
    return -ENOMEM;
  }
  int res = networkfs_http_call(parent->i_sb->s_fs_info, type, NULL, 0, 2,
                                "parent", number, "name", escaped_name);
  kfree(escaped_name);
  return res == 0 ? 0 : -1;
}

int networkfs_mkdir(struct user_namespace *user_ns, struct inode *parent,
                    struct dentry *child, umode_t mode) {
  return create_http_call(child, parent, mode, S_IFDIR);
}

int networkfs_rmdir(struct inode *parent, struct dentry *child) {
  return remove_http_call(parent, child, "rmdir");
}

int networkfs_unlink(struct inode *parent, struct dentry *child) {
  return remove_http_call(parent, child, "unlink");
}

int networkfs_create(struct user_namespace *user_ns, struct inode *parent,
                     struct dentry *child, umode_t mode, bool b) {
  return create_http_call(child, parent, mode, S_IFREG);
}

int networkfs_iterate(struct file *filp, struct dir_context *ctx) {
  struct dentry *dentry = filp->f_path.dentry;
  struct inode *inode = d_inode(dentry);
  struct entries *response =
      (struct entries *)kzalloc(sizeof(struct entries), GFP_KERNEL);
  if (response == NULL) {
    return -ENOMEM;
  }
  char number[8];
  sprintf(number, "%lu", inode->i_ino);
  int res =
      networkfs_http_call(inode->i_sb->s_fs_info, "list", (char *)response,
                          sizeof(*response), 1, "inode", number);
  if (res != 0) {
    kfree(response);
    return -1;
  }
  loff_t record_counter = 0;
  if (ctx->pos < response->entries_count ||
      (response->entries_count == 0 && ctx->pos == 0)) {
    dir_emit(ctx, ".", 1, inode->i_ino, DT_DIR);
    struct inode *parent_inode = dentry->d_parent->d_inode;
    dir_emit(ctx, "..", 2, parent_inode->i_ino, DT_DIR);
  }
  if (response->entries_count == 0 && ctx->pos == 0) {
    ctx->pos++;
  }
  while (ctx->pos < response->entries_count) {
    dir_emit(ctx, response->entries[ctx->pos].name,
             strlen(response->entries[ctx->pos].name),
             response->entries[ctx->pos].ino,
             response->entries[ctx->pos].entry_type);
    record_counter++;
    ctx->pos++;
  }
  kfree(response);
  return record_counter;
}

void networkfs_kill_sb(struct super_block *sb) {
  printk(KERN_INFO "networkfs: superblock is destroyed %s",
         (char *)sb->s_fs_info);
  kfree(sb->s_fs_info);
}

int networkfs_fill_super(struct super_block *sb, struct fs_context *fc) {
  // Создаём корневую inode
  struct inode *inode = networkfs_get_inode(sb, NULL, S_IFDIR, 1000);

  // Создаём корень файловой системы
  sb->s_root = d_make_root(inode);
  sb->s_fs_info = (char *)kzalloc(strlen(fc->source), GFP_KERNEL);
  sb->s_maxbytes = MAX_BYTES;
  if (sb->s_root == NULL || sb->s_fs_info == NULL) {
    return -ENOMEM;
  }
  memcpy(sb->s_fs_info, fc->source, strlen(fc->source));
  return 0;
}

int networkfs_get_tree(struct fs_context *fc) {
  int ret = get_tree_nodev(fc, networkfs_fill_super);

  if (ret != 0) {
    printk(KERN_ERR "networkfs: unable to mount: error code %d", ret);
  }

  return ret;
}

struct dentry *networkfs_lookup(struct inode *parent, struct dentry *child,
                                unsigned int flag) {
  const char *name = child->d_name.name;
  struct entry_info *response = &(struct entry_info){0};
  char number[8];
  sprintf(number, "%lu", parent->i_ino);
  char *escaped_name = escape_name(name, strlen(name));
  if (escaped_name == NULL) {
    return NULL;
  }
  int res = networkfs_http_call(parent->i_sb->s_fs_info, "lookup",
                                (char *)response, sizeof(*response), 2,
                                "parent", number, "name", escaped_name);
  kfree(escaped_name);
  if (res != 0) {
    return NULL;
  }
  struct inode *inode = networkfs_get_inode(
      parent->i_sb, parent,
      (response->entry_type == DT_DIR ? S_IFDIR : S_IFREG), response->ino);
  d_add(child, inode);
  return NULL;
}

struct file_operations networkfs_dir_ops = {.iterate = networkfs_iterate,
                                            .open = networkfs_open,
                                            .read = networkfs_read,
                                            .write = networkfs_write,
                                            .flush = networkfs_flush,
                                            .fsync = networkfs_fsync,
                                            .release = networkfs_release,
                                            .llseek = generic_file_llseek};

struct inode_operations networkfs_inode_ops = {.lookup = networkfs_lookup,
                                               .create = networkfs_create,
                                               .unlink = networkfs_unlink,
                                               .rmdir = networkfs_rmdir,
                                               .mkdir = networkfs_mkdir,
                                               .setattr = networkfs_setattr,
                                               .link = networkfs_link};

struct inode *networkfs_get_inode(struct super_block *sb,
                                  const struct inode *parent, umode_t mode,
                                  int i_ino) {
  struct inode *inode;
  inode = new_inode(sb);

  if (inode != NULL) {
    inode->i_ino = i_ino;
    inode->i_fop = &networkfs_dir_ops;
    inode->i_op = &networkfs_inode_ops;
    inode->i_size = 0;
    inode_init_owner(&init_user_ns, inode, parent,
                     mode | S_IRWXU | S_IRWXG | S_IRWXO);
  }

  return inode;
}

struct fs_context_operations networkfs_context_ops = {.get_tree =
                                                          networkfs_get_tree};

int networkfs_init_fs_context(struct fs_context *fc) {
  fc->ops = &networkfs_context_ops;
  return 0;
}

struct file_system_type networkfs_fs_type = {
    .name = "networkfs",
    .kill_sb = networkfs_kill_sb,
    .init_fs_context = networkfs_init_fs_context};

int networkfs_init(void) {
  struct fs_context fsc = {0};
  networkfs_init_fs_context(&fsc);
  int ret_code = register_filesystem(&networkfs_fs_type);
  if (ret_code != 0) {
    return ret_code;
  }
  printk(KERN_INFO "Hello, World!\n");
  return 0;
}

void networkfs_exit(void) {
  int ret_code = unregister_filesystem(&networkfs_fs_type);
  if (ret_code != 0) {
    printk(KERN_ERR "Cannot load\n");
  }
  printk(KERN_INFO "Goodbye!\n");
}

module_init(networkfs_init);
module_exit(networkfs_exit);
