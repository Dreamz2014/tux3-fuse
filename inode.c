/*
 * Tux3 versioning filesystem in user space
 *
 * Original copyright (c) 2008 Daniel Phillips <phillips@phunq.net>
 * Licensed under the GPL version 3
 *
 * By contributing changes to this file you grant the original copyright holder
 * the right to distribute those changes under any license.
 */

#include "tux3user.h"

#ifndef trace
#define trace trace_on
#endif

#define HASH_SHIFT	10
#define HASH_SIZE	(1 << 10)
#define HASH_MASK	(HASH_SIZE - 1)

static struct hlist_head inode_hashtable[HASH_SIZE] = {
	[0 ... (HASH_SIZE - 1)] = HLIST_HEAD_INIT,
};

static unsigned long hash(inum_t inum)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return hash_64(inum, HASH_SHIFT);
}

void inode_leak_check(void)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	int leaks = 0;

	for (int i = 0; i < HASH_SIZE; i++) {
		struct hlist_head *head = inode_hashtable + i;
		struct inode *inode;
		hlist_for_each_entry(inode, head, i_hash) {
			trace_on("possible leak inode inum %Lu, i_count %d",
				 tux_inode(inode)->inum,
				 atomic_read(&inode->i_count));
			leaks++;
		}
	}

	assert(leaks == 0);
}

static inline int inode_unhashed(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return hlist_unhashed(&inode->i_hash);
}

static void insert_inode_hash(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct hlist_head *b = inode_hashtable + hash(tux_inode(inode)->inum);
	hlist_add_head(&inode->i_hash, b);
}

void remove_inode_hash(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	if (!inode_unhashed(inode))
		hlist_del_init(&inode->i_hash);
}

static struct inode *new_inode(struct sb *sb)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct tux3_inode *tuxnode;
	struct inode *inode;

	tuxnode = malloc(sizeof(*tuxnode));
	if (!tuxnode)
		goto error;

	inode_init(tuxnode, sb, 0);
	inode = &tuxnode->vfs_inode;

	inode->map = new_map(sb->dev, NULL);
	if (!inode->map)
		goto error_map;

	inode->map->inode = inode;

	return inode;

error_map:
	free(tuxnode);
error:
	return NULL;
}

static void free_inode(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct tux3_inode *tuxnode = tux_inode(inode);

	inode->i_state &= ~I_BAD;

	free_inode_check(tuxnode);

	free_map(mapping(inode));
	free(tuxnode);
}

/* This is just to clean inode is partially initialized */
static void make_bad_inode(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	remove_inode_hash(inode);
	inode->i_state |= I_BAD;
}

static int is_bad_inode(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return inode->i_state & I_BAD;
}

void unlock_new_inode(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(inode->i_state & I_NEW);
	inode->i_state &= ~I_NEW;
}

static void iget_failed(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	make_bad_inode(inode);
	unlock_new_inode(inode);
	iput(inode);
}

void __iget(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(!(inode->i_state & I_FREEING));
	if (atomic_read(&inode->i_count)) {
		atomic_inc(&inode->i_count);
		return;
	}
	/* i_count == 0 should happen only dirty inode */
	assert(inode->i_state & I_DIRTY);
	atomic_inc(&inode->i_count);
}

/* This is used by tux3_clear_dirty_inodes() to tell inode state was changed */
void iget_if_dirty(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	__iget(inode);
}

/* get additional reference to inode; caller must already hold one. */
void ihold(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(!(inode->i_state & I_FREEING));
	assert(atomic_read(&inode->i_count) >= 1);
	atomic_inc(&inode->i_count);
}

static struct inode *find_inode(struct sb *sb, struct hlist_head *head,
				int (*test)(struct inode *, void *),
				void *data)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct inode *inode;

	hlist_for_each_entry(inode, head, i_hash) {
		if (test(inode, data)) {
			__iget(inode);
			return inode;
		}
	}
	return NULL;
}

static struct inode *ilookup5_nowait(struct sb *sb, inum_t inum,
		int (*test)(struct inode *, void *), void *data)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct hlist_head *head = inode_hashtable + hash(inum);
	struct inode *inode;

	inode = find_inode(sb, head, test, data);

	return inode;
}

static struct inode *ilookup5(struct sb *sb, inum_t inum,
		int (*test)(struct inode *, void *), void *data)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct inode *inode = ilookup5_nowait(sb, inum, test, data);
	/* On userland, inode shouldn't have I_NEW */
	assert(!inode || !(inode->i_state & I_NEW));
	return inode;
}

static struct inode *iget5_locked(struct sb *sb, inum_t inum,
			   int (*test)(struct inode *, void *),
			   int (*set)(struct inode *, void *), void *data)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct hlist_head *head = inode_hashtable + hash(inum);
	struct inode *inode;

	inode = find_inode(sb, head, test, data);
	if (inode)
		return inode;

	inode = new_inode(sb);
	if (!inode)
		return NULL;
	if (set(inode, data)) {
		free_inode(inode);
		return NULL;
	}

	inode->i_state = I_NEW;
	hlist_add_head(&inode->i_hash, head);

	return inode;
}

static int insert_inode_locked4(struct inode *inode, inum_t inum,
			 int (*test)(struct inode *, void *), void *data)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct hlist_head *head = inode_hashtable + hash(inum);

	while (1) {
		struct inode *old = NULL;

		hlist_for_each_entry(old, head, i_hash) {
			if (!test(old, data))
				continue;
			if (old->i_state & (I_FREEING /*|I_WILL_FREE*/))
				continue;
			break;
		}
		if (likely(!old)) {
			inode->i_state |= I_NEW;
			hlist_add_head(&inode->i_hash, head);
			return 0;
		}
		__iget(old);
		/* wait_on_inode(old); */
		if (unlikely(!inode_unhashed(old))) {
			iput(old);
			return -EBUSY;
		}
		iput(old);
	}
}

loff_t i_size_read(const struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return inode->i_size;
}

void i_size_write(struct inode *inode, loff_t i_size)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	inode->i_size = i_size;
}

/* Truncate partial block. If partial, we have to update last block. */
static int tux3_truncate_partial_block(struct inode *inode, loff_t newsize)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned delta = tux3_get_current_delta();
	struct sb *sb = tux_sb(inode->i_sb);
	block_t index = newsize >> sb->blockbits;
	unsigned offset = newsize & sb->blockmask;
	struct buffer_head *buffer, *clone;

	if (!offset)
		return 0;

	buffer = blockread(mapping(inode), index);
	if (!buffer)
		return -EIO;

	clone = blockdirty(buffer, delta);
	if (IS_ERR(clone)) {
		blockput(buffer);
		return PTR_ERR(clone);
	}

	memset(bufdata(clone) + offset, 0, sb->blocksize - offset);
	mark_buffer_dirty_non(clone);
	blockput(clone);

	return 0;
}

/* For now, we doesn't cache inode */
static int generic_drop_inode(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return 1;
}

#include "kernel/inode.c"

static void tux_setup_inode(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct sb *sb = tux_sb(inode->i_sb);

	assert(tux_inode(inode)->inum != TUX_INVALID_INO);
	switch (inode->i_mode & S_IFMT) {
	case S_IFSOCK:
	case S_IFIFO:
	case S_IFBLK:
	case S_IFCHR:
		inode->map->io = dev_errio;
		break;
	case S_IFREG:
//		inode->map->io = tux3_filemap_overwrite_io;
		inode->map->io = tux3_filemap_redirect_io;
		break;
	case S_IFDIR:
	case S_IFLNK:
		inode->map->io = tux3_filemap_redirect_io;
		break;
	case 0: /* internal inode */
		/* FIXME: bitmap, logmap, vtable, atable doesn't have S_IFMT */
		switch (tux_inode(inode)->inum) {
		case TUX_BITMAP_INO:
		case TUX_VTABLE_INO:
		case TUX_ATABLE_INO:
			/* set fake i_size to escape the check of block_* */
			inode->i_size = vfs_sb(sb)->s_maxbytes;
			inode->map->io = tux3_filemap_redirect_io;
			/* Flushed by tux3_flush_inode_internal() */
			tux3_set_inode_no_flush(inode);
			break;
		case TUX_VOLMAP_INO:
		case TUX_LOGMAP_INO:
			inode->i_size = (loff_t)sb->volblocks << sb->blockbits;
			if (tux_inode(inode)->inum == TUX_VOLMAP_INO)
				/* use default handler (dev_blockio) */;
			else
				inode->map->io = tux3_logmap_io;
			/* Flushed by tux3_flush_inode_internal() */
			tux3_set_inode_no_flush(inode);
			break;
		default:
			assert(0);
			break;
		}
		break;
	default:
		tux3_fs_error(sb, "Unknown mode: inum %Lx, mode %07ho",
			      tux_inode(inode)->inum, inode->i_mode);
		break;
	}
}

/*
 * NOTE: iput() must not be called inside of change_begin/end() if
 * i_nlink == 0.  Otherwise, it will become cause of deadlock.
 */
void iput(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	if (inode == NULL)
		return;

	if (atomic_dec_and_test(&inode->i_count)) {
		assert(!(inode->i_state & I_NEW));

		if (!tux3_drop_inode(inode)) {
			/* Keep the inode on dirty list */
			return;
		}

		tux3_evict_inode(inode);

		remove_inode_hash(inode);
		free_inode(inode);
	}
}

int __tuxtruncate(struct inode *inode, loff_t size)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return tux3_truncate(inode, size);
}

int tuxtruncate(struct inode *inode, loff_t size)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct sb *sb = tux_sb(inode->i_sb);
	int err;

	change_begin(sb);
	tux3_iattrdirty(inode);
	err = __tuxtruncate(inode, size);
	change_end(sb);

	return err;
}
