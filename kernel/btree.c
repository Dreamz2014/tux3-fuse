/*
 * Generic btree operations
 *
 * Original copyright (c) 2008 Daniel Phillips <phillips@phunq.net>
 * Portions copyright (c) 2006-2008 Google Inc.
 * Licensed under the GPL version 2
 *
 * By contributing changes to this file you grant the original copyright holder
 * the right to distribute those changes under any license.
 */

#include "tux3.h"

#ifndef trace
#define trace trace_off
#endif

/* This value is special case to tell btree doesn't have root yet. */
struct root no_root = {
	.block	= 0,
	.depth	= 0,
};

struct bnode {
	__be16 magic;
	__be16 unused;
	__be32 count;
	struct index_entry {
		__be64 key;
		__be64 block;
	} entries[];
};

/*
 * Note that the first key of an index block is never accessed.  This is
 * because for a btree, there is always one more key than nodes in each
 * index node.  In other words, keys lie between node pointers.  I
 * micro-optimize by placing the node count in the first key, which allows
 * a node to contain an esthetically pleasing binary number of pointers.
 * (Not done yet.)
 */

unsigned calc_entries_per_node(unsigned blocksize)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return (blocksize - sizeof(struct bnode)) / sizeof(struct index_entry);
}

static inline unsigned bcount(struct bnode *node)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return be32_to_cpu(node->count);
}

static struct buffer_head *new_block(struct btree *btree)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct block_segment seg;

	int err = btree->ops->balloc(btree->sb, 1, &seg, 1);
	if (err)
		return ERR_PTR(err);
	struct buffer_head *buffer = vol_getblk(btree->sb, seg.block);
	if (!buffer)
		return ERR_PTR(-ENOMEM); // ERR_PTR me!!! and bfree?
	return buffer;
}

struct buffer_head *new_leaf(struct btree *btree)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct buffer_head *buffer = new_block(btree);

	if (!IS_ERR(buffer)) {
		memset(bufdata(buffer), 0, bufsize(buffer));
		(btree->ops->leaf_init)(btree, bufdata(buffer));
		mark_buffer_dirty_atomic(buffer);
	}
	return buffer;
}

static inline void bnode_buffer_init(struct buffer_head *buffer)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct bnode *bnode = bufdata(buffer);
	memset(bnode, 0, bufsize(buffer));
	bnode->magic = cpu_to_be16(TUX3_MAGIC_BNODE);
}

static inline int bnode_sniff(struct bnode *bnode)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return bnode->magic == cpu_to_be16(TUX3_MAGIC_BNODE);
}

static struct buffer_head *new_node(struct btree *btree)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct buffer_head *buffer = new_block(btree);

	if (!IS_ERR(buffer)) {
		bnode_buffer_init(buffer);
		mark_buffer_unify_atomic(buffer);
	}
	return buffer;
}

/*
 * A btree cursor has n + 1 entries for a btree of depth n, with the first n
 * entries pointing at internal nodes and entry n + 1 pointing at a leaf.
 * The next field points at the next index entry that will be loaded in a left
 * to right tree traversal, not the current entry.  The next pointer is null
 * for the leaf, which has its own specialized traversal algorithms.
 */

static inline struct bnode *level_node(struct cursor *cursor, int level)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return bufdata(cursor->path[level].buffer);
}

struct buffer_head *cursor_leafbuf(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(cursor->level == cursor->btree->root.depth);
	return cursor->path[cursor->level].buffer;
}

static void cursor_root_add(struct cursor *cursor, struct buffer_head *buffer,
			    struct index_entry *next)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	
#ifdef CURSOR_DEBUG
	assert(cursor->level < cursor->maxlevel);
	assert(cursor->path[cursor->level + 1].buffer == FREE_BUFFER);
	assert(cursor->path[cursor->level + 1].next == FREE_NEXT);
#endif
	vecmove(cursor->path + 1, cursor->path, cursor->level + 1);
	cursor->level++;
	cursor->path[0].buffer = buffer;
	cursor->path[0].next = next;
}

static void level_replace_blockput(struct cursor *cursor, int level,
				   struct buffer_head *buffer,
				   struct index_entry *next)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
#ifdef CURSOR_DEBUG
	assert(buffer);
	assert(level <= cursor->level);
	assert(cursor->path[level].buffer != FREE_BUFFER);
	assert(cursor->path[level].next != FREE_NEXT);
#endif
	blockput(cursor->path[level].buffer);
	cursor->path[level].buffer = buffer;
	cursor->path[level].next = next;
}

static void cursor_push(struct cursor *cursor, struct buffer_head *buffer,
			struct index_entry *next)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	cursor->level++;
#ifdef CURSOR_DEBUG
	assert(cursor->level <= cursor->maxlevel);
	assert(cursor->path[cursor->level].buffer == FREE_BUFFER);
	assert(cursor->path[cursor->level].next == FREE_NEXT);
#endif
	cursor->path[cursor->level].buffer = buffer;
	cursor->path[cursor->level].next = next;
}

static struct buffer_head *cursor_pop(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct buffer_head *buffer;

#ifdef CURSOR_DEBUG
	assert(cursor->level >= 0);
#endif
	buffer = cursor->path[cursor->level].buffer;
#ifdef CURSOR_DEBUG
	cursor->path[cursor->level].buffer = FREE_BUFFER;
	cursor->path[cursor->level].next = FREE_NEXT;
#endif
	cursor->level--;
	return buffer;
}

static inline void cursor_pop_blockput(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	blockput(cursor_pop(cursor));
}

/* There is no next entry? */
static inline int level_finished(struct cursor *cursor, int level)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct bnode *node = level_node(cursor, level);
	return cursor->path[level].next == node->entries + bcount(node);
}
// also write level_beginning!!!

void release_cursor(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	while (cursor->level >= 0)
		cursor_pop_blockput(cursor);
}

/* unused */
void show_cursor(struct cursor *cursor, int depth)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	__tux3_dbg(">>> cursor %p/%i:", cursor, depth);
	for (int i = 0; i < depth; i++) {
		__tux3_dbg(" [%Lx/%i]",
			   bufindex(cursor->path[i].buffer),
			   bufcount(cursor->path[i].buffer));
	}
	__tux3_dbg("\n");
}

static void cursor_check(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	if (cursor->level == -1)
		return;
	tuxkey_t key = 0;
	block_t block = cursor->btree->root.block;

	for (int i = 0; i <= cursor->level; i++) {
		assert(bufindex(cursor->path[i].buffer) == block);
		if (i == cursor->level)
			break;

		struct bnode *bnode = level_node(cursor, i);
		struct index_entry *entry = cursor->path[i].next - 1;
		assert(bnode->entries <= entry);
		assert(entry < bnode->entries + bcount(bnode));
		/*
		 * If this entry is most left, it should be same key
		 * with parent. Otherwise, most left key may not be
		 * correct as next key.
		 */
		if (bnode->entries == entry)
			assert(be64_to_cpu(entry->key) == key);
		else
			assert(be64_to_cpu(entry->key) > key);

		block = be64_to_cpu(entry->block);
		key = be64_to_cpu(entry->key);
	}
}

static inline int alloc_cursor_size(int count)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return sizeof(struct cursor) + sizeof(struct path_level) * count;
}

struct cursor *alloc_cursor(struct btree *btree, int extra)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	int maxlevel = btree->root.depth + extra;
	struct cursor *cursor = malloc(alloc_cursor_size(maxlevel + 1));

	if (cursor) {
		cursor->btree = btree;
		cursor->level = -1;
#ifdef CURSOR_DEBUG
		cursor->maxlevel = maxlevel;
		for (int i = 0; i <= maxlevel; i++) {
			cursor->path[i].buffer = FREE_BUFFER; /* for debug */
			cursor->path[i].next = FREE_NEXT; /* for debug */
		}
#endif
	}
	return cursor;
}

void free_cursor(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
#ifdef CURSOR_DEBUG
	assert(cursor->level == -1);
#endif
	free(cursor);
}

/* Lookup the index entry contains key */
static struct index_entry *bnode_lookup(struct bnode *node, tuxkey_t key)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct index_entry *next = node->entries, *top = next + bcount(node);
	assert(bcount(node) > 0);
	/* binary search goes here */
	while (++next < top) {
		if (be64_to_cpu(next->key) > key)
			break;
	}
	return next - 1;
}

static int cursor_level_finished(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* must not be leaf */
	assert(cursor->level < cursor->btree->root.depth);
	return level_finished(cursor, cursor->level);
}

/*
 * Climb up the cursor until we find the first level where we have not yet read
 * all the way to the end of the index block, there we find the key that
 * separates the subtree we are in (a leaf) from the next subtree to the right.
 */
tuxkey_t cursor_next_key(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	int level = cursor->level;
	assert(cursor->level == cursor->btree->root.depth);
	while (level--) {
		if (!level_finished(cursor, level))
			return be64_to_cpu(cursor->path[level].next->key);
	}
	return TUXKEY_LIMIT;
}

static tuxkey_t cursor_level_next_key(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	int level = cursor->level;
	assert(cursor->level < cursor->btree->root.depth);
	while (level >= 0) {
		if (!level_finished(cursor, level))
			return be64_to_cpu(cursor->path[level].next->key);
		level--;
	}
	return TUXKEY_LIMIT;
}

/* Return key of this leaf */
tuxkey_t cursor_this_key(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(cursor->level == cursor->btree->root.depth);
	return be64_to_cpu((cursor->path[cursor->level - 1].next - 1)->key);
}

static tuxkey_t cursor_level_this_key(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(cursor->level < cursor->btree->root.depth);
	return be64_to_cpu((cursor->path[cursor->level].next - 1)->key);
}

/*
 * Cursor read root node.
 * < 0 - error
 *   0 - success
 */
static int cursor_read_root(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct btree *btree = cursor->btree;
	struct buffer_head *buffer;

	assert(has_root(btree));

	buffer = vol_bread(btree->sb, btree->root.block);
	if (!buffer)
		return -EIO; /* FIXME: stupid, it might have been NOMEM */
	assert(bnode_sniff(bufdata(buffer)));
	cursor_push(cursor, buffer, ((struct bnode *)bufdata(buffer))->entries);
	return 0;
}

/*
 * Cursor up to parent node.
 * 0 - there is no further parent (root was popped)
 * 1 - there is parent
 */
static int cursor_advance_up(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(cursor->level >= 0);
	cursor_pop_blockput(cursor);
	return cursor->level >= 0;
}

/*
 * Cursor down to child node or leaf, and update ->next.
 * < 0 - error
 *   0 - there is no further child (leaf was pushed)
 *   1 - there is child
 */
static int cursor_advance_down(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct btree *btree = cursor->btree;
	struct buffer_head *buffer;
	block_t child;

	assert(cursor->level < btree->root.depth);

	child = be64_to_cpu(cursor->path[cursor->level].next->block);
	buffer = vol_bread(btree->sb, child);
	if (!buffer)
		return -EIO; /* FIXME: stupid, it might have been NOMEM */
	cursor->path[cursor->level].next++;

	if (cursor->level < btree->root.depth - 1) {
		struct bnode *node = bufdata(buffer);
		assert(bnode_sniff(node));
		cursor_push(cursor, buffer, node->entries);
		cursor_check(cursor);
		return 1;
	}

	assert(btree->ops->leaf_sniff(btree, bufdata(buffer)));
	cursor_push(cursor, buffer, NULL);
	cursor_check(cursor);
	return 0;
}

/*
 * Cursor advance for btree traverse.
 * < 0 - error
 *   0 - Finished traverse
 *   1 - Reached leaf
 */
static int cursor_advance(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	int ret;

	do {
		if (!cursor_advance_up(cursor))
			return 0;
	} while (cursor_level_finished(cursor));
	do {
		ret = cursor_advance_down(cursor);
		if (ret < 0)
			return ret;
	} while (ret);

	return 1;
}

/* Lookup index and set it as next down path */
static void cursor_bnode_lookup(struct cursor *cursor, tuxkey_t key)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct path_level *at = &cursor->path[cursor->level];
	at->next = bnode_lookup(bufdata(at->buffer), key);
}

int btree_probe(struct cursor *cursor, tuxkey_t key)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	int ret;

	ret = cursor_read_root(cursor);
	if (ret < 0)
		return ret;
	do {
		cursor_bnode_lookup(cursor, key);

		ret = cursor_advance_down(cursor);
		if (ret < 0)
			goto error;
	} while (ret);

	return 0;

error:
	release_cursor(cursor);
	return ret;
}

/*
 * Traverse btree for specified range
 * key: start to traverse (cursor should point leaf is including key)
 * len: length to traverse
 *
 * return value:
 * < 0 - error
 *   0 - traversed all range
 * 0 < - traverse was stopped by func, and return value of func
 */
int btree_traverse(struct cursor *cursor, tuxkey_t key, u64 len,
		   btree_traverse_func_t func, void *data)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct btree *btree = cursor->btree;
	int ret;

	do {
		tuxkey_t bottom = cursor_this_key(cursor);
		tuxkey_t limit = cursor_next_key(cursor);
		void *leaf = bufdata(cursor_leafbuf(cursor));
		assert(btree->ops->leaf_sniff(btree, leaf));

		if (key < bottom) {
			len -= min_t(u64, len, bottom - key);
			if (len == 0)
				break;
			key = bottom;
		}

		ret = func(btree, bottom, limit, leaf, key, len, data);
		/* Stop traverse if ret >= 1, or error */
		if (ret)
			goto out;

		/* If next key is out of range, done */
		if (key + len <= limit)
			break;

		ret = cursor_advance(cursor);
		if (ret < 0)
			goto out;
	} while (ret);

	ret = 0;
out:
	return ret;
}

void show_tree_range(struct btree *btree, tuxkey_t start, unsigned count)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	__tux3_dbg("%i level btree at %Li:\n",
		   btree->root.depth, btree->root.block);
	if (!has_root(btree))
		return;

	struct cursor *cursor = alloc_cursor(btree, 0);
	if (!cursor) {
		tux3_err(btree->sb, "out of memory");
		return;
	}
	if (btree_probe(cursor, start)) {
		tux3_fs_error(btree->sb, "tell me why!!!");
		goto out;
	}

	struct buffer_head *buffer;
	do {
		buffer = cursor_leafbuf(cursor);
		assert((btree->ops->leaf_sniff)(btree, bufdata(buffer)));
		(btree->ops->leaf_dump)(btree, bufdata(buffer));
	} while (--count && cursor_advance(cursor));

out:
	free_cursor(cursor);
}

void show_tree(struct btree *btree)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	show_tree_range(btree, 0, -1);
}

static void level_redirect_blockput(struct cursor *cursor, int level, struct buffer_head *clone)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct buffer_head *buffer = cursor->path[level].buffer;
	struct index_entry *next = cursor->path[level].next;

	/* If this level has ->next, update ->next to the clone buffer */
	if (next)
		next = ptr_redirect(next, bufdata(buffer), bufdata(clone));

	memcpy(bufdata(clone), bufdata(buffer), bufsize(clone));
	level_replace_blockput(cursor, level, clone, next);
}

static int leaf_need_redirect(struct sb *sb, struct buffer_head *buffer)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* FIXME: leaf doesn't have delta number, we might want to
	 * remove exception for leaf */
	/* If this is not re-dirty, we need to redirect */
	return !buffer_dirty(buffer);
}

static int bnode_need_redirect(struct sb *sb, struct buffer_head *buffer)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* If this is not re-dirty for sb->unify, we need to redirect */
	return !buffer_already_dirty(buffer, sb->unify);
}

/*
 * Recursively redirect non-dirty buffers on path to modify leaf.
 *
 * Redirect order is from root to leaf. Otherwise, blocks of path will
 * be allocated by reverse order.
 *
 * FIXME: We can allocate/copy blocks before change common ancestor
 * (before changing common ancestor, changes are not visible for
 * reader). With this, we may be able to reduce locking time.
 */
int cursor_redirect(struct cursor *cursor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct btree *btree = cursor->btree;
	struct sb *sb = btree->sb;
	int level;

	for (level = 0; level <= btree->root.depth; level++) {
		struct buffer_head *buffer, *clone;
		block_t parent, oldblock, newblock;
		struct index_entry *entry;
		int redirect, is_leaf = (level == btree->root.depth);

		buffer = cursor->path[level].buffer;
		/* If buffer needs to redirect to dirty, redirect it */
		if (is_leaf)
			redirect = leaf_need_redirect(sb, buffer);
		else
			redirect = bnode_need_redirect(sb, buffer);

		/* No need to redirect */
		if (!redirect)
			continue;

		/* Redirect buffer before changing */
		clone = new_block(btree);
		if (IS_ERR(clone))
			return PTR_ERR(clone);
		oldblock = bufindex(buffer);
		newblock = bufindex(clone);
		trace("redirect %Lx to %Lx", oldblock, newblock);
		level_redirect_blockput(cursor, level, clone);
		if (is_leaf) {
			/* This is leaf buffer */
			mark_buffer_dirty_atomic(clone);
			log_leaf_redirect(sb, oldblock, newblock);
			defer_bfree(&sb->defree, oldblock, 1);
		} else {
			/* This is bnode buffer */
			mark_buffer_unify_atomic(clone);
			log_bnode_redirect(sb, oldblock, newblock);
			defer_bfree(&sb->deunify, oldblock, 1);
		}

		trace("update parent");
		if (!level) {
			/* Update pointer in btree->root */
			trace("redirect root");
			assert(oldblock == btree->root.block);
			btree->root.block = newblock;
			tux3_mark_btree_dirty(btree);
			continue;
		}
		/* Update entry on parent for the redirected block */
		parent = bufindex(cursor->path[level - 1].buffer);
		entry = cursor->path[level - 1].next - 1;
		entry->block = cpu_to_be64(newblock);
		log_bnode_update(sb, parent, newblock, be64_to_cpu(entry->key));
	}

	cursor_check(cursor);
	return 0;
}

/* Deletion */

static void bnode_remove_index(struct bnode *node, struct index_entry *p,
			       int count)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned total = bcount(node);
	void *end = node->entries + total;
	memmove(p, p + count, end - (void *)(p + count));
	node->count = cpu_to_be32(total - count);
}

static int bnode_merge_nodes(struct sb *sb, struct bnode *into,
			     struct bnode *from)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned into_count = bcount(into), from_count = bcount(from);

	if (from_count + into_count > sb->entries_per_node)
		return 0;

	veccopy(&into->entries[into_count], from->entries, from_count);
	into->count = cpu_to_be32(into_count + from_count);

	return 1;
}

static void adjust_parent_sep(struct cursor *cursor, int level, __be64 newsep)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* Update separating key until nearest common parent */
	while (level >= 0) {
		struct path_level *parent_at = &cursor->path[level];
		struct index_entry *parent = parent_at->next - 1;

		assert(0 < be64_to_cpu(parent->key));
		assert(be64_to_cpu(parent->key) < be64_to_cpu(newsep));
		log_bnode_adjust(cursor->btree->sb,
				 bufindex(parent_at->buffer),
				 be64_to_cpu(parent->key),
				 be64_to_cpu(newsep));
		parent->key = newsep;
		mark_buffer_unify_non(parent_at->buffer);

		if (parent != level_node(cursor, level)->entries)
			break;

		level--;
	}
}

/* Tracking info for chopped bnode indexes */
struct chopped_index_info {
	tuxkey_t start;
	int count;
};

static void remove_index(struct cursor *cursor, struct chopped_index_info *cii)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	int level = cursor->level;
	struct bnode *node = level_node(cursor, level);
	struct chopped_index_info *ciil = &cii[level];

	/* Collect chopped index in this node for logging later */
	if (!ciil->count)
		ciil->start = be64_to_cpu((cursor->path[level].next - 1)->key);
	ciil->count++;

	/* Remove an index */
	bnode_remove_index(node, cursor->path[level].next - 1, 1);
	--(cursor->path[level].next);
	mark_buffer_unify_non(cursor->path[level].buffer);

	/*
	 * Climb up to common parent and update separating key.
	 *
	 * What if index is now empty?  (no deleted key)
	 *
	 * Then some key above is going to be deleted and used to set sep
	 * Climb the cursor while at first entry, bail out at root find the
	 * node with the old sep, set it to deleted key
	 */

	/* There is no separator for last entry or root node */
	if (!level || cursor_level_finished(cursor))
		return;
	/* If removed index was not first entry, no change to separator */
	if (cursor->path[level].next != node->entries)
		return;

	adjust_parent_sep(cursor, level - 1, cursor->path[level].next->key);
}

static int try_leaf_merge(struct btree *btree, struct buffer_head *intobuf,
			  struct buffer_head *frombuf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct vleaf *from = bufdata(frombuf);
	struct vleaf *into = bufdata(intobuf);

	/* Try to merge leaves */
	if (btree->ops->leaf_merge(btree, into, from)) {
		struct sb *sb = btree->sb;
		/*
		 * We know frombuf is redirected and dirty. So, in
		 * here, we can just cancel leaf_redirect by bfree(),
		 * instead of defered_bfree()
		 * FIXME: we can optimize freeing leaf without
		 * leaf_redirect, and if we did, this is not true.
		 */
		bfree(sb, bufindex(frombuf), 1);
		log_leaf_free(sb, bufindex(frombuf));
		return 1;
	}
	return 0;
}

static int try_bnode_merge(struct sb *sb, struct buffer_head *intobuf,
			   struct buffer_head *frombuf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct bnode *into = bufdata(intobuf);
	struct bnode *from = bufdata(frombuf);

	/* Try to merge nodes */
	if (bnode_merge_nodes(sb, into, from)) {
		/*
		 * We know frombuf is redirected and dirty. So, in
		 * here, we can just cancel bnode_redirect by bfree(),
		 * instead of defered_bfree()
		 * FIXME: we can optimize freeing bnode without
		 * bnode_redirect, and if we did, this is not true.
		 */
		bfree(sb, bufindex(frombuf), 1);
		log_bnode_merge(sb, bufindex(frombuf), bufindex(intobuf));
		return 1;
	}
	return 0;
}

/*
 * This is range deletion. So, instead of adjusting balance of the
 * space on sibling nodes for each change, this just removes the range
 * and merges from right to left even if it is not same parent.
 *
 *              +--------------- (A, B, C)--------------------+
 *              |                    |                        |
 *     +-- (AA, AB, AC) -+       +- (BA, BB, BC) -+      + (CA, CB, CC) +
 *     |        |        |       |        |       |      |       |      |
 * (AAA,AAB)(ABA,ABB)(ACA,ACB) (BAA,BAB)(BBA)(BCA,BCB)  (CAA)(CBA,CBB)(CCA)
 *
 * [less : A, AA, AAA, AAB, AB, ABA, ABB, AC, ACA, ACB, B, BA ... : greater]
 *
 * If we merged from cousin (or re-distributed), we may have to update
 * the index until common parent. (e.g. removed (ACB), then merged
 * from (BAA,BAB) to (ACA), we have to adjust B in root node to BB)
 *
 * See, adjust_parent_sep().
 *
 * FIXME: no re-distribute. so, we don't guarantee above than 50%
 * space efficiency. And if range is end of key (truncate() case), we
 * don't need to merge, and adjust_parent_sep().
 *
 * FIXME2: we may want to split chop work for each step. instead of
 * blocking for a long time.
 */
int btree_chop(struct btree *btree, tuxkey_t start, u64 len)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct sb *sb = btree->sb;
	struct btree_ops *ops = btree->ops;
	struct buffer_head **prev, *leafprev = NULL;
	struct chopped_index_info *cii;
	struct cursor *cursor;
	tuxkey_t limit;
	int ret, done = 0;

	if (!has_root(btree))
		return 0;

	/* Chop all range if len >= TUXKEY_LIMIT */
	limit = (len >= TUXKEY_LIMIT) ? TUXKEY_LIMIT : start + len;

	prev = malloc(sizeof(*prev) * btree->root.depth);
	if (prev == NULL)
		return -ENOMEM;
	memset(prev, 0, sizeof(*prev) * btree->root.depth);

	cii = malloc(sizeof(*cii) * btree->root.depth);
	if (cii == NULL) {
		ret = -ENOMEM;
		goto error_cii;
	}
	memset(cii, 0, sizeof(*cii) * btree->root.depth);

	cursor = alloc_cursor(btree, 0);
	if (!cursor) {
		ret = -ENOMEM;
		goto error_alloc_cursor;
	}

	down_write(&btree->lock);
	ret = btree_probe(cursor, start);
	if (ret)
		goto error_btree_probe;

	/* Walk leaves */
	while (1) {
		struct buffer_head *leafbuf;
		tuxkey_t this_key;

		/*
		 * FIXME: If leaf was merged and freed later, we don't
		 * need to redirect leaf and leaf_chop()
		 */
		if ((ret = cursor_redirect(cursor)))
			goto out;
		leafbuf = cursor_pop(cursor);

		/* Adjust start and len for this leaf */
		this_key = cursor_level_this_key(cursor);
		if (start < this_key) {
			if (limit < TUXKEY_LIMIT)
				len -= this_key - start;
			start = this_key;
		}

		ret = ops->leaf_chop(btree, start, len, bufdata(leafbuf));
		if (ret) {
			if (ret < 0) {
				blockput(leafbuf);
				goto out;
			}
			mark_buffer_dirty_non(leafbuf);
		}

		/* Try to merge this leaf with prev */
		if (leafprev) {
			if (try_leaf_merge(btree, leafprev, leafbuf)) {
				trace(">>> can merge leaf %p into leaf %p", leafbuf, leafprev);
				remove_index(cursor, cii);
				mark_buffer_dirty_non(leafprev);
				blockput_free(sb, leafbuf);
				goto keep_prev_leaf;
			}
			blockput(leafprev);
		}
		leafprev = leafbuf;

keep_prev_leaf:

		if (cursor_level_next_key(cursor) >= limit)
			done = 1;
		/* Pop and try to merge finished nodes */
		while (done || cursor_level_finished(cursor)) {
			struct buffer_head *buf;
			int level = cursor->level;
			struct chopped_index_info *ciil = &cii[level];


			/* Get merge src buffer, and go parent level */
			buf = cursor_pop(cursor);

			/*
			 * Logging chopped indexes
			 * FIXME: If node is freed later (e.g. merged),
			 * we dont't need to log this
			 */
			if (ciil->count) {
				log_bnode_del(sb, bufindex(buf), ciil->start,
					      ciil->count);
			}
			memset(ciil, 0, sizeof(*ciil));

			/* Try to merge node with prev */
			if (prev[level]) {
				assert(level);
				if (try_bnode_merge(sb, prev[level], buf)) {
					trace(">>> can merge node %p into node %p", buf, prev[level]);
					remove_index(cursor, cii);
					mark_buffer_unify_non(prev[level]);
					blockput_free_unify(sb, buf);
					goto keep_prev_node;
				}
				blockput(prev[level]);
			}
			prev[level] = buf;
keep_prev_node:

			if (!level)
				goto chop_root;
		}

		/* Push back down to leaf level */
		do {
			ret = cursor_advance_down(cursor);
			if (ret < 0)
				goto out;
		} while (ret);
	}

chop_root:
	/* Remove depth if possible */
	while (btree->root.depth > 1 && bcount(bufdata(prev[0])) == 1) {
		trace("drop btree level");
		btree->root.block = bufindex(prev[1]);
		btree->root.depth--;
		tux3_mark_btree_dirty(btree);

		/*
		 * We know prev[0] is redirected and dirty. So, in
		 * here, we can just cancel bnode_redirect by bfree(),
		 * instead of defered_bfree()
		 * FIXME: we can optimize freeing bnode without
		 * bnode_redirect, and if we did, this is not true.
		 */
		bfree(sb, bufindex(prev[0]), 1);
		log_bnode_free(sb, bufindex(prev[0]));
		blockput_free_unify(sb, prev[0]);

		vecmove(prev, prev + 1, btree->root.depth);
	}
	ret = 0;

out:
	if (leafprev)
		blockput(leafprev);
	for (int i = 0; i < btree->root.depth; i++) {
		if (prev[i])
			blockput(prev[i]);
	}
	release_cursor(cursor);
error_btree_probe:
	up_write(&btree->lock);

	free_cursor(cursor);
error_alloc_cursor:
	free(cii);
error_cii:
	free(prev);

	return ret;
}

/* root must be initialized by zero */
static void bnode_init_root(struct bnode *root, unsigned count, block_t left,
			    block_t right, tuxkey_t rkey)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	root->count		= cpu_to_be32(count);
	root->entries[0].block	= cpu_to_be64(left);
	root->entries[1].block	= cpu_to_be64(right);
	root->entries[1].key	= cpu_to_be64(rkey);
}

/* Insertion */

static void bnode_add_index(struct bnode *node, struct index_entry *p,
			    block_t child, u64 childkey)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned count = bcount(node);
	vecmove(p + 1, p, node->entries + count - p);
	p->block	= cpu_to_be64(child);
	p->key		= cpu_to_be64(childkey);
	node->count	= cpu_to_be32(count + 1);
}

static void bnode_split(struct bnode *src, unsigned pos, struct bnode *dst)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	dst->count = cpu_to_be32(bcount(src) - pos);
	src->count = cpu_to_be32(pos);

	memcpy(&dst->entries[0], &src->entries[pos],
	       bcount(dst) * sizeof(struct index_entry));
}

/*
 * Insert new leaf to next cursor position.
 * keep == 1: keep current cursor position.
 * keep == 0, set cursor position to new leaf.
 */
static int insert_leaf(struct cursor *cursor, tuxkey_t childkey, struct buffer_head *leafbuf, int keep)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct btree *btree = cursor->btree;
	struct sb *sb = btree->sb;
	int level = btree->root.depth;
	block_t childblock = bufindex(leafbuf);

	if (keep)
		blockput(leafbuf);
	else {
		cursor_pop_blockput(cursor);
		cursor_push(cursor, leafbuf, NULL);
	}
	while (level--) {
		struct path_level *at = &cursor->path[level];
		struct buffer_head *parentbuf = at->buffer;
		struct bnode *parent = bufdata(parentbuf);

		/* insert and exit if not full */
		if (bcount(parent) < btree->sb->entries_per_node) {
			bnode_add_index(parent, at->next, childblock, childkey);
			if (!keep)
				at->next++;
			log_bnode_add(sb, bufindex(parentbuf), childblock, childkey);
			mark_buffer_unify_non(parentbuf);
			cursor_check(cursor);
			return 0;
		}

		/* split a full index node */
		struct buffer_head *newbuf = new_node(btree);
		if (IS_ERR(newbuf))
			return PTR_ERR(newbuf);

		struct bnode *newnode = bufdata(newbuf);
		unsigned half = bcount(parent) / 2;
		u64 newkey = be64_to_cpu(parent->entries[half].key);

		bnode_split(parent, half, newnode);
		log_bnode_split(sb, bufindex(parentbuf), half, bufindex(newbuf));

		/* if the cursor is in the new node, use that as the parent */
		int child_is_left = at->next <= parent->entries + half;
		if (!child_is_left) {
			struct index_entry *newnext;
			mark_buffer_unify_non(parentbuf);
			newnext = newnode->entries + (at->next - &parent->entries[half]);
			get_bh(newbuf);
			level_replace_blockput(cursor, level, newbuf, newnext);
			parentbuf = newbuf;
			parent = newnode;
		} else
			mark_buffer_unify_non(newbuf);

		bnode_add_index(parent, at->next, childblock, childkey);
		if (!keep)
			at->next++;
		log_bnode_add(sb, bufindex(parentbuf), childblock, childkey);
		mark_buffer_unify_non(parentbuf);

		childkey = newkey;
		childblock = bufindex(newbuf);
		blockput(newbuf);

		/*
		 * If child is in left bnode, we should keep the
		 * cursor position to child, otherwise adjust cursor
		 * to new bnode.
		 */
		keep = child_is_left;
	}

	/* Make new root bnode */
	trace("add tree level");
	struct buffer_head *newbuf = new_node(btree);
	if (IS_ERR(newbuf))
		return PTR_ERR(newbuf);

	struct bnode *newroot = bufdata(newbuf);
	block_t newrootblock = bufindex(newbuf);
	block_t oldrootblock = btree->root.block;
	int left_node = bufindex(cursor->path[0].buffer) != childblock;
	bnode_init_root(newroot, 2, oldrootblock, childblock, childkey);
	cursor_root_add(cursor, newbuf, newroot->entries + 1 + !left_node);
	log_bnode_root(sb, newrootblock, 2, oldrootblock, childblock, childkey);

	/* Change btree to point the new root */
	btree->root.block = newrootblock;
	btree->root.depth++;

	mark_buffer_unify_non(newbuf);
	tux3_mark_btree_dirty(btree);
	cursor_check(cursor);

	return 0;
}

/* Insert new leaf to next cursor position, then set cursor to new leaf */
int btree_insert_leaf(struct cursor *cursor, tuxkey_t key, struct buffer_head *leafbuf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return insert_leaf(cursor, key, leafbuf, 0);
}

/*
 * Split leaf, then insert to parent.
 * key:  key to add after split (cursor will point leaf which is including key)
 * hint: hint for split
 *
 * return value:
 *   0 - success
 * < 0 - error
 */
static int btree_leaf_split(struct cursor *cursor, tuxkey_t key, tuxkey_t hint)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	trace("split leaf");
	struct btree *btree = cursor->btree;
	struct buffer_head *newbuf;

	newbuf = new_leaf(btree);
	if (IS_ERR(newbuf))
		return PTR_ERR(newbuf);
	log_balloc(btree->sb, bufindex(newbuf), 1);

	struct buffer_head *leafbuf = cursor_leafbuf(cursor);
	tuxkey_t newkey = btree->ops->leaf_split(btree, hint, bufdata(leafbuf),
						 bufdata(newbuf));
	assert(cursor_this_key(cursor) < newkey);
	assert(newkey < cursor_next_key(cursor));
	if (key < newkey)
		mark_buffer_dirty_non(newbuf);
	else
		mark_buffer_dirty_non(leafbuf);
	return insert_leaf(cursor, newkey, newbuf, key < newkey);
}

int btree_write(struct cursor *cursor, struct btree_key_range *key)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct btree *btree = cursor->btree;
	struct btree_ops *ops = btree->ops;
	tuxkey_t split_hint;
	int err;

	/* FIXME: we might be better to support multiple leaves */

	err = cursor_redirect(cursor);
	if (err)
		return err;

	while (key->len > 0) {
		tuxkey_t bottom = cursor_this_key(cursor);
		tuxkey_t limit = cursor_next_key(cursor);
		void *leaf = bufdata(cursor_leafbuf(cursor));
		int need_split;

		assert(bottom <= key->start && key->start < limit);
		assert(ops->leaf_sniff(btree, leaf));

		need_split = ops->leaf_write(btree, bottom, limit, leaf, key,
					     &split_hint);
		if (need_split < 0)
			return need_split;
		else if (!need_split) {
			mark_buffer_dirty_non(cursor_leafbuf(cursor));
			continue;
		}

		err = btree_leaf_split(cursor, key->start, split_hint);
		if (err)
			return err;	/* FIXME: error handling */
	}

	return 0;
}

int btree_read(struct cursor *cursor, struct btree_key_range *key)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct btree *btree = cursor->btree;
	struct btree_ops *ops = btree->ops;
	void *leaf = bufdata(cursor_leafbuf(cursor));
	tuxkey_t bottom = cursor_this_key(cursor);
	tuxkey_t limit = cursor_next_key(cursor);

	/* FIXME: we might be better to support multiple leaves */

	assert(bottom <= key->start && key->start < limit);
	assert(ops->leaf_sniff(btree, leaf));

	return ops->leaf_read(btree, bottom, limit, leaf, key);
}

void init_btree(struct btree *btree, struct sb *sb, struct root root, struct btree_ops *ops)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	btree->sb = sb;
	btree->ops = ops;
	btree->root = root;
	init_rwsem(&btree->lock);
	ops->btree_init(btree);
}

int alloc_empty_btree(struct btree *btree)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct sb *sb = btree->sb;
	struct buffer_head *rootbuf = new_node(btree);
	if (IS_ERR(rootbuf))
		goto error;
	struct buffer_head *leafbuf = new_leaf(btree);
	if (IS_ERR(leafbuf))
		goto error_leafbuf;

	assert(!has_root(btree));
	struct bnode *rootnode = bufdata(rootbuf);
	block_t rootblock = bufindex(rootbuf);
	block_t leafblock = bufindex(leafbuf);
	trace("root at %Lx", rootblock);
	trace("leaf at %Lx", leafblock);
	bnode_init_root(rootnode, 1, leafblock, 0, 0);
	log_bnode_root(sb, rootblock, 1, leafblock, 0, 0);
	log_balloc(sb, leafblock, 1);

	mark_buffer_unify_non(rootbuf);
	blockput(rootbuf);
	mark_buffer_dirty_non(leafbuf);
	blockput(leafbuf);

	btree->root = (struct root){ .block = rootblock, .depth = 1 };
	tux3_mark_btree_dirty(btree);

	return 0;

error_leafbuf:
	(btree->ops->bfree)(sb, bufindex(rootbuf), 1);
	blockput(rootbuf);
	rootbuf = leafbuf;
error:
	return PTR_ERR(rootbuf);
}

/* FIXME: right? and this should be done by btree_chop()? */
int free_empty_btree(struct btree *btree)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct btree_ops *ops = btree->ops;

	if (!has_root(btree))
		return 0;

	assert(btree->root.depth == 1);
	struct sb *sb = btree->sb;
	struct buffer_head *rootbuf = vol_bread(sb, btree->root.block);
	if (!rootbuf)
		return -EIO;
	assert(bnode_sniff(bufdata(rootbuf)));
	/* Make btree has no root */
	btree->root = no_root;
	tux3_mark_btree_dirty(btree);

	struct bnode *rootnode = bufdata(rootbuf);
	assert(bcount(rootnode) == 1);
	block_t leaf = be64_to_cpu(rootnode->entries[0].block);
	struct buffer_head *leafbuf = vol_find_get_block(sb, leaf);

	if (leafbuf && !leaf_need_redirect(sb, leafbuf)) {
		/*
		 * This is redirected leaf. So, in here, we can just
		 * cancel leaf_redirect by bfree(), instead of
		 * defered_bfree().
		 */
		bfree(sb, leaf, 1);
		log_leaf_free(sb, leaf);
		assert(ops->leaf_can_free(btree, bufdata(leafbuf)));
		blockput_free(sb, leafbuf);
	} else {
		defer_bfree(&sb->defree, leaf, 1);
		log_bfree(sb, leaf, 1);
		if (leafbuf) {
			assert(ops->leaf_can_free(btree, bufdata(leafbuf)));
			blockput(leafbuf);
		}
	}

	if (!bnode_need_redirect(sb, rootbuf)) {
		/*
		 * This is redirected bnode. So, in here, we can just
		 * cancel bnode_redirect by bfree(), instead of
		 * defered_bfree().
		 */
		bfree(sb, bufindex(rootbuf), 1);
		log_bnode_free(sb, bufindex(rootbuf));
		blockput_free_unify(sb, rootbuf);
	} else {
		defer_bfree(&sb->deunify, bufindex(rootbuf), 1);
		log_bfree_on_unify(sb, bufindex(rootbuf), 1);
		blockput(rootbuf);
	}

	return 0;
}

int replay_bnode_redirect(struct replay *rp, block_t oldblock, block_t newblock)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct sb *sb = rp->sb;
	struct buffer_head *newbuf, *oldbuf;
	int err = 0;

	newbuf = vol_getblk(sb, newblock);
	if (!newbuf) {
		err = -ENOMEM;	/* FIXME: error code */
		goto error;
	}
	oldbuf = vol_bread(sb, oldblock);
	if (!oldbuf) {
		err = -EIO;	/* FIXME: error code */
		goto error_put_newbuf;
	}
	assert(bnode_sniff(bufdata(oldbuf)));

	memcpy(bufdata(newbuf), bufdata(oldbuf), bufsize(newbuf));
	mark_buffer_unify_atomic(newbuf);

	blockput(oldbuf);
error_put_newbuf:
	blockput(newbuf);
error:
	return err;
}

int replay_bnode_root(struct replay *rp, block_t root, unsigned count,
		      block_t left, block_t right, tuxkey_t rkey)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct sb *sb = rp->sb;
	struct buffer_head *rootbuf;

	rootbuf = vol_getblk(sb, root);
	if (!rootbuf)
		return -ENOMEM;
	bnode_buffer_init(rootbuf);

	bnode_init_root(bufdata(rootbuf), count, left, right, rkey);

	mark_buffer_unify_atomic(rootbuf);
	blockput(rootbuf);

	return 0;
}

/*
 * Before this replay, replay should already dirty the buffer of src.
 * (e.g. by redirect)
 */
int replay_bnode_split(struct replay *rp, block_t src, unsigned pos,
		       block_t dst)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct sb *sb = rp->sb;
	struct buffer_head *srcbuf, *dstbuf;
	int err = 0;

	srcbuf = vol_getblk(sb, src);
	if (!srcbuf) {
		err = -ENOMEM;	/* FIXME: error code */
		goto error;
	}

	dstbuf = vol_getblk(sb, dst);
	if (!dstbuf) {
		err = -ENOMEM;	/* FIXME: error code */
		goto error_put_srcbuf;
	}
	bnode_buffer_init(dstbuf);

	bnode_split(bufdata(srcbuf), pos, bufdata(dstbuf));

	mark_buffer_unify_non(srcbuf);
	mark_buffer_unify_atomic(dstbuf);

	blockput(dstbuf);
error_put_srcbuf:
	blockput(srcbuf);
error:
	return err;
}

/*
 * Before this replay, replay should already dirty the buffer of bnodeblock.
 * (e.g. by redirect)
 */
static int replay_bnode_change(struct sb *sb, block_t bnodeblock,
			       u64 val1, u64 val2,
			       void (*change)(struct bnode *, u64, u64))
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct buffer_head *bnodebuf;

	bnodebuf = vol_getblk(sb, bnodeblock);
	if (!bnodebuf)
		return -ENOMEM;	/* FIXME: error code */

	struct bnode *bnode = bufdata(bnodebuf);
	change(bnode, val1, val2);

	mark_buffer_unify_non(bnodebuf);
	blockput(bnodebuf);

	return 0;
}

static void add_func(struct bnode *bnode, u64 child, u64 key)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct index_entry *entry = bnode_lookup(bnode, key) + 1;
	bnode_add_index(bnode, entry, child, key);
}

int replay_bnode_add(struct replay *rp, block_t parent, block_t child,
		     tuxkey_t key)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return replay_bnode_change(rp->sb, parent, child, key, add_func);
}

static void update_func(struct bnode *bnode, u64 child, u64 key)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct index_entry *entry = bnode_lookup(bnode, key);
	assert(be64_to_cpu(entry->key) == key);
	entry->block = cpu_to_be64(child);
}

int replay_bnode_update(struct replay *rp, block_t parent, block_t child,
			tuxkey_t key)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return replay_bnode_change(rp->sb, parent, child, key, update_func);
}

int replay_bnode_merge(struct replay *rp, block_t src, block_t dst)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct sb *sb = rp->sb;
	struct buffer_head *srcbuf, *dstbuf;
	int err = 0, ret;

	srcbuf = vol_getblk(sb, src);
	if (!srcbuf) {
		err = -ENOMEM;	/* FIXME: error code */
		goto error;
	}

	dstbuf = vol_getblk(sb, dst);
	if (!dstbuf) {
		err = -ENOMEM;	/* FIXME: error code */
		goto error_put_srcbuf;
	}

	ret = bnode_merge_nodes(sb, bufdata(dstbuf), bufdata(srcbuf));
	assert(ret == 1);

	mark_buffer_unify_non(dstbuf);
	mark_buffer_unify_non(srcbuf);

	blockput(dstbuf);
error_put_srcbuf:
	blockput(srcbuf);
error:
	return err;
}

static void del_func(struct bnode *bnode, u64 key, u64 count)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct index_entry *entry = bnode_lookup(bnode, key);
	assert(be64_to_cpu(entry->key) == key);
	bnode_remove_index(bnode, entry, count);
}

int replay_bnode_del(struct replay *rp, block_t bnode, tuxkey_t key,
		     unsigned count)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return replay_bnode_change(rp->sb, bnode, key, count, del_func);
}

static void adjust_func(struct bnode *bnode, u64 from, u64 to)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct index_entry *entry = bnode_lookup(bnode, from);
	assert(be64_to_cpu(entry->key) == from);
	entry->key = cpu_to_be64(to);
}

int replay_bnode_adjust(struct replay *rp, block_t bnode, tuxkey_t from,
			tuxkey_t to)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return replay_bnode_change(rp->sb, bnode, from, to, adjust_func);
}
