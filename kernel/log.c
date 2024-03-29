/*
 * Copyright (c) 2008, Daniel Phillips
 * Copyright (c) 2008, OGAWA Hirofumi
 */


#include "tux3.h"

#ifndef trace
#define trace trace_on
#endif

/*
 * Log cache scheme
 *
 *  - The purpose of the log is to reconstruct pinned metadata that has become
 *    dirty since the last log flush, in case of shutdown without log flush.
 *
 *  - Log blocks are cached in the page cache mapping of an internal inode,
 *    sb->logmap.  The inode itself is not used, just the mapping, so with a
 *    some work we could create/destroy the mapping by itself without the inode.
 *
 *  - There is no direct mapping from the log block cache to physical disk,
 *    instead there is a reverse chain starting from sb->logchain.  Log blocks
 *    are read only at replay on mount and written only at delta transition.
 *
 *  - sb->super.logcount: count of log blocks in unify cycle
 *  - sb->lognext: Logmap index of next log block in delta cycle
 *  - sb->logpos/logtop: Pointer/limit to write next log entry
 *  - sb->logbuf: Cached log block referenced by logpos/logtop
 *
 *  - At delta staging, physical addresses are assigned for log blocks from
 *    0 to lognext, reverse chain pointers are set in the log blocks, and
 *    all log blocks for the delta are submitted for writeout.
 *
 *  - At delta commit, count of log blocks is recorded in superblock
 *    (later, metablock) which are the log blocks for the current
 *    unify cycle.
 *
 *  - On delta completion, if log was unified in current delta then log blocks
 *    are freed for reuse.  Log blocks to be freed are recorded in sb->deunify,
 *    which is appended to sb->defree, the per-delta deferred free list at log
 *    flush time.
 *
 *  - On replay, sb.super->logcount log blocks for current unify cycle are
 *    loaded in reverse order into logmap, using the log block reverse chain
 *    pointers.
 *
 * Log block format
 *
 *  - Each log block has a header and one or more variable sized entries,
 *    serially encoded.
 *
 *  - Format and handling of log block entries is similar to inode attributes.
 *
 *  - Log block header records size of log block payload in ->bytes.
 *
 *  - Each log block entry has a one byte type code implying its length.
 *
 *  - Integer fields are big endian, byte aligned.
 *
 * Log block locking
 *
 *  - Log block must be touched only by the backend. So, we don't need to lock.
 */

unsigned log_size[] = {
	[LOG_BALLOC]		= 11,
	[LOG_BFREE]		= 11,
	[LOG_BFREE_ON_UNIFY]	= 11,
	[LOG_BFREE_RELOG]	= 11,
	[LOG_LEAF_REDIRECT]	= 13,
	[LOG_LEAF_FREE]		= 7,
	[LOG_BNODE_REDIRECT]	= 13,
	[LOG_BNODE_ROOT]	= 26,
	[LOG_BNODE_SPLIT]	= 15,
	[LOG_BNODE_ADD]		= 19,
	[LOG_BNODE_UPDATE]	= 19,
	[LOG_BNODE_MERGE]	= 13,
	[LOG_BNODE_DEL]		= 15,
	[LOG_BNODE_ADJUST]	= 19,
	[LOG_BNODE_FREE]	= 7,
	[LOG_ORPHAN_ADD]	= 9,
	[LOG_ORPHAN_DEL]	= 9,
	[LOG_FREEBLOCKS]	= 7,
	[LOG_UNIFY]		= 1,
	[LOG_DELTA]		= 1,
};

void log_next(struct sb *sb)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* FIXME: error handling of blockget() */
	sb->logbuf = blockget(mapping(sb->logmap), sb->lognext++);
	sb->logpos = bufdata(sb->logbuf) + sizeof(struct logblock);
	sb->logtop = bufdata(sb->logbuf) + sb->blocksize;
}

void log_drop(struct sb *sb)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	blockput(sb->logbuf);
	sb->logbuf = NULL;
	sb->logtop = sb->logpos = NULL;
}

void log_finish(struct sb *sb)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	if (sb->logbuf) {
		struct logblock *log = bufdata(sb->logbuf);
		assert(sb->logtop >= sb->logpos);
		log->bytes = cpu_to_be16(sb->logpos - log->data);
		memset(sb->logpos, 0, sb->logtop - sb->logpos);
		log_drop(sb);
	}
}

void log_finish_cycle(struct sb *sb, int discard)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* ->logbuf must be finished */
	assert(sb->logbuf == NULL);

	if (discard) {
		struct buffer_head *logbuf;
		unsigned i, logcount = sb->lognext;

		/* Clear dirty of buffer */
		for (i = 0; i < logcount; i++) {
			logbuf = blockget(mapping(sb->logmap), i);
			blockput_free(sb, logbuf);
		}
	}

	/* Initialize for new delta cycle */
	sb->lognext = 0;
}

static void *log_begin(struct sb *sb, unsigned bytes)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(tux3_under_backend(sb));
	if (sb->logpos + bytes > sb->logtop) {
		log_finish(sb);
		log_next(sb);

		*(struct logblock *)bufdata(sb->logbuf) = (struct logblock){
			.magic = cpu_to_be16(TUX3_MAGIC_LOG),
		};

		/* Dirty for write, and prevent to be reclaimed */
		mark_buffer_dirty_atomic(sb->logbuf);
	}
	return sb->logpos;
}

static void log_end(struct sb *sb, void *pos)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	sb->logpos = pos;
}

/*
 * Flush log blocks.
 *
 * Add log blocks to ->logchain list and adjust ->logcount. Then
 * flush log blocks at once.
 */
int tux3_logmap_io(int rw, struct bufvec *bufvec)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct inode *logmap = bufvec_inode(bufvec);
	struct sb *sb = tux_sb(logmap->i_sb);
	unsigned count = bufvec_contig_count(bufvec);

	assert(rw & WRITE);
	assert(bufvec_contig_index(bufvec) == 0);

	while (count > 0) {
		struct buffer_head *buffer;
		struct block_segment seg;
		block_t block, limit;
		int err;

		err = balloc_partial(sb, count, &seg, 1);
		if (err) {
			assert(err);
			return err;
		}

		/*
		 * Link log blocks to logchain.
		 *
		 * FIXME: making the link for each block is
		 * inefficient to read on replay. Instead, we would be
		 * able to use the link of extent. With it, we can
		 * read multiple blocks at once.
		 */
		block = seg.block;
		limit = seg.block + seg.count;
		bufvec_buffer_for_each_contig(buffer, bufvec) {
			struct logblock *log = bufdata(buffer);

			assert(log->magic == cpu_to_be16(TUX3_MAGIC_LOG));
			log->logchain = sb->super.logchain;

			trace("logchain %lld", block);
			sb->super.logchain = cpu_to_be64(block);
			block++;
			if (block == limit)
				break;
		}

		err = __tux3_volmap_io(rw, bufvec, seg.block, seg.count);
		if (err) {
			tux3_err(sb, "logblock write error (%d)", err);
			return err;	/* FIXME: error handling */
		}

		/*
		 * We can obsolete the log blocks after next unify
		 * by LOG_BFREE_RELOG.
		 */
		defer_bfree(&sb->deunify, seg.block, seg.count);

		/* Add count of log on this delta to unify logcount */
		be32_add_cpu(&sb->super.logcount, seg.count);

		count -= seg.count;
	}

	return 0;
}

static void log_intent(struct sb *sb, u8 intent)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* Check whether array is uptodate */
	BUILD_BUG_ON(ARRAY_SIZE(log_size) != LOG_TYPES);

	unsigned char *data = log_begin(sb, 1);
	*data++ = intent;
	log_end(sb, data);
}

static void log_u48(struct sb *sb, u8 intent, u64 v1)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned char *data = log_begin(sb, log_size[intent]);
	*data++ = intent;
	log_end(sb, encode48(data, v1));
}

static void log_u16_u48(struct sb *sb, u8 intent, u16 v1, u64 v2)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned char *data = log_begin(sb, log_size[intent]);
	*data++ = intent;
	data = encode16(data, v1);
	log_end(sb, encode48(data, v2));
}

static void log_u32_u48(struct sb *sb, u8 intent, u32 v1, u64 v2)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned char *data = log_begin(sb, log_size[intent]);
	*data++ = intent;
	data = encode32(data, v1);
	log_end(sb, encode48(data, v2));
}

static void log_u48_u48(struct sb *sb, u8 intent, u64 v1, u64 v2)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned char *data = log_begin(sb, log_size[intent]);
	*data++ = intent;
	data = encode48(data, v1);
	log_end(sb, encode48(data, v2));
}

static void log_u16_u48_u48(struct sb *sb, u8 intent, u16 v1, u64 v2, u64 v3)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned char *data = log_begin(sb, log_size[intent]);
	*data++ = intent;
	data = encode16(data, v1);
	data = encode48(data, v2);
	log_end(sb, encode48(data, v3));
}

static void log_u48_u48_u48(struct sb *sb, u8 intent, u64 v1, u64 v2, u64 v3)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned char *data = log_begin(sb, log_size[intent]);
	*data++ = intent;
	data = encode48(data, v1);
	data = encode48(data, v2);
	log_end(sb, encode48(data, v3));
}

/* balloc() until next unify */
void log_balloc(struct sb *sb, block_t block, unsigned count)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* FIXME: 32bits count is too big? */
	log_u32_u48(sb, LOG_BALLOC, count, block);
}

/* bfree() */
void log_bfree(struct sb *sb, block_t block, unsigned count)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* FIXME: 32bits count is too big? */
	log_u32_u48(sb, LOG_BFREE, count, block);
}

/* Defered bfree() until after next unify */
void log_bfree_on_unify(struct sb *sb, block_t block, unsigned count)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* FIXME: 32bits count is too big? */
	log_u32_u48(sb, LOG_BFREE_ON_UNIFY, count, block);
}

/* Same with log_bfree() (re-logged log_bfree_on_unify() on unify) */
void log_bfree_relog(struct sb *sb, block_t block, unsigned count)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* FIXME: 32bits count is too big? */
	log_u32_u48(sb, LOG_BFREE_RELOG, count, block);
}

/*
 * 1. balloc(newblock) until next unify
 * 2. bfree(oldblock)
 */
void log_leaf_redirect(struct sb *sb, block_t oldblock, block_t newblock)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_u48_u48(sb, LOG_LEAF_REDIRECT, oldblock, newblock);
}

/* Same with log_bfree(leaf) (but this is for canceling log_leaf_redirect()) */
void log_leaf_free(struct sb *sb, block_t leaf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_u48(sb, LOG_LEAF_FREE, leaf);
}

/*
 * 1. Redirect from oldblock to newblock
 * 2. balloc(newblock) until next unify
 * 2. Defered bfree(oldblock) until after next unify
 */
void log_bnode_redirect(struct sb *sb, block_t oldblock, block_t newblock)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_u48_u48(sb, LOG_BNODE_REDIRECT, oldblock, newblock);
}

/*
 * 1. Construct root buffer until next unify
 * 2. balloc(root) until next unify
 */
/* The left key should always be 0 on new root */
void log_bnode_root(struct sb *sb, block_t root, unsigned count,
		    block_t left, block_t right, tuxkey_t rkey)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned char *data = log_begin(sb, log_size[LOG_BNODE_ROOT]);
	assert(count == 1 || count == 2);
	*data++ = LOG_BNODE_ROOT;
	*data++ = count;
	data = encode48(data, root);
	data = encode48(data, left);
	data = encode48(data, right);
	log_end(sb, encode48(data, rkey));
}

/*
 * 1. Split bnode from src to dst until next unify
 * 2. balloc(dst) until next unify
 * (src buffer must be dirty already)
 */
void log_bnode_split(struct sb *sb, block_t src, unsigned pos, block_t dst)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_u16_u48_u48(sb, LOG_BNODE_SPLIT, pos, src, dst);
}

/*
 * Insert new index (child, key) to parent until next unify
 * (parent buffer must be dirty already)
 */
void log_bnode_add(struct sb *sb, block_t parent, block_t child, tuxkey_t key)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_u48_u48_u48(sb, LOG_BNODE_ADD, parent, child, key);
}

/*
 * Update ->block of "key" index by child on parent until next unify
 * (parent buffer must be dirty already)
 */
void log_bnode_update(struct sb *sb, block_t parent, block_t child, tuxkey_t key)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_u48_u48_u48(sb, LOG_BNODE_UPDATE, parent, child, key);
}

/*
 * 1. Merge btree nodes from src to dst until next unify
 * 2. bfree(src) (but this is for canceling log_bnode_redirect())
 * 3. Clear dirty of src buffer
 * (src and dst buffers must be dirty already)
 */
void log_bnode_merge(struct sb *sb, block_t src, block_t dst)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_u48_u48(sb, LOG_BNODE_MERGE, src, dst);
}

/*
 * Delete indexes specified by (key, count) in bnode until next unify
 * (bnode buffer must be dirty already)
 */
void log_bnode_del(struct sb *sb, block_t bnode, tuxkey_t key, unsigned count)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_u16_u48_u48(sb, LOG_BNODE_DEL, count, bnode, key);
}

/*
 * Adjust ->key of index specified by "from" to "to" until next unify
 * (bnode buffer must be dirty already)
 */
void log_bnode_adjust(struct sb *sb, block_t bnode, tuxkey_t from, tuxkey_t to)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_u48_u48_u48(sb, LOG_BNODE_ADJUST, bnode, from, to);
}

/*
 * 1. bfree(bnode)  (but this is for canceling log_bnode_redirect())
 * 2. Clear dirty of bnode buffer
 * (bnode must be dirty already)
 */
void log_bnode_free(struct sb *sb, block_t bnode)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_u48(sb, LOG_BNODE_FREE, bnode);
}

/*
 * Handle inum as orphan inode
 * (this is log of frontend operation)
 */
void log_orphan_add(struct sb *sb, unsigned version, tuxkey_t inum)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_u16_u48(sb, LOG_ORPHAN_ADD, version, inum);
}

/*
 * Handle inum as destroyed
 * (this is log of frontend operation)
 */
void log_orphan_del(struct sb *sb, unsigned version, tuxkey_t inum)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_u16_u48(sb, LOG_ORPHAN_DEL, version, inum);
}

/* Current freeblocks on unify */
void log_freeblocks(struct sb *sb, block_t freeblocks)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_u48(sb, LOG_FREEBLOCKS, freeblocks);
}

/* Log to know where is new unify cycle  */
void log_unify(struct sb *sb)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_intent(sb, LOG_UNIFY);
}

/* Just add log record as delta mark (for debugging) */
void log_delta(struct sb *sb)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	log_intent(sb, LOG_DELTA);
}

/* Stash infrastructure (struct stash must be initialized by zero clear) */

/*
 * Stash utility - store an arbitrary number of u64 values in a linked queue
 * of pages.
 */

static inline struct link *page_link(struct page *page)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return (void *)&page->private;
}

void stash_init(struct stash *stash)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	init_flink_head(&stash->head);
	stash->pos = stash->top = NULL;
}

/* Add new entry (value) to stash */
int stash_value(struct stash *stash, u64 value)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	if (stash->pos == stash->top) {
		struct page *page = alloc_page(GFP_NOFS);
		if (!page)
			return -ENOMEM;
		stash->top = page_address(page) + PAGE_SIZE;
		stash->pos = page_address(page);
		if (!flink_empty(&stash->head))
			flink_add(page_link(page), &stash->head);
		else
			flink_first_add(page_link(page), &stash->head);
	}
	*stash->pos++ = value;
	return 0;
}

/* Free all pages in stash to empty. */
static void empty_stash(struct stash *stash)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct flink_head *head = &stash->head;

	if (!flink_empty(head)) {
		struct page *page;
		while (1) {
			page = __flink_next_entry(head, struct page, private);
			if (flink_is_last(head))
				break;
			flink_del_next(head);
			__free_page(page);
		}
		__free_page(page);
		stash_init(stash);
	}
}

/*
 * Call actor() for each entries. And, prepare to add new entry to stash.
 * (NOTE: after this, stash keeps one page for future stash_value().)
 */
int unstash(struct sb *sb, struct stash *stash, unstash_t actor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct flink_head *head = &stash->head;
	struct page *page;

	if (flink_empty(head))
		return 0;
	while (1) {
		page = __flink_next_entry(head, struct page, private);
		u64 *vec = page_address(page);
		u64 *top = page_address(page) + PAGE_SIZE;

		if (top == stash->top)
			top = stash->pos;
		for (; vec < top; vec++) {
			int err;
			if ((err = actor(sb, *vec)))
				return err;
		}
		if (flink_is_last(head))
			break;
		flink_del_next(head);
		__free_page(page);
	}
	stash->pos = page_address(page);
	return 0;
}

/*
 * Call actor() for each entries without freeing pages.
 */
int stash_walk(struct sb *sb, struct stash *stash, unstash_t actor)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct flink_head *head = &stash->head;
	struct page *page;

	if (flink_empty(head))
		return 0;

	struct link *link, *first;
	link = first = flink_next(head);
	do {
		page = __link_entry(link, struct page, private);
		u64 *vec = page_address(page);
		u64 *top = page_address(page) + PAGE_SIZE;

		if (top == stash->top)
			top = stash->pos;
		for (; vec < top; vec++) {
			int err;
			if ((err = actor(sb, *vec)))
				return err;
		}

		link = link->next;
	} while (link != first);

	return 0;
}

/* Deferred free blocks list */

int defer_bfree(struct stash *defree, block_t block, unsigned count)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	static const unsigned limit = ULLONG_MAX >> 48;

	/*
	 * count field of stash is 16bits. So, this separates to
	 * multiple records to avoid overflow.
	 */
	while (count) {
		unsigned c = min(count, limit);
		int err;

		err = stash_value(defree, ((u64)c << 48) + block);
		if (err)
			return err;

		count -= c;
		block += c;
	}

	return 0;
}

void destroy_defer_bfree(struct stash *defree)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	empty_stash(defree);
}
