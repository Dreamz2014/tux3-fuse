/*
 * Inode btree leaf operations
 *
 * Original copyright (c) 2008 Daniel Phillips <phillips@phunq.net>
 * Licensed under the GPL version 2
 *
 * By contributing changes to this file you grant the original copyright holder
 * the right to distribute those changes under any license.
 */


#include "tux3.h"
#include "ileaf.h"

#ifndef trace
#define trace trace_on
#endif

struct ileaf {
	__be16 magic;		/* Magic number */
	__be16 count;		/* Counts of used offset info entries */
	u32 pad;
	__be64 ibase;		/* Base inode number */
	char table[];		/* ileaf data: inode attrs ... offset info */
};

/*
 * inode leaf format
 *
 * A leaf has a small header followed by a table of attributes.  A vector of
 * offsets within the block grows down from the top of the leaf towards the
 * top of the attribute table, indexed by the difference between inum and
 * leaf->ibase, the base inum of the table block.
 */

static inline __be16 *ileaf_dict(struct btree *btree, struct ileaf *ileaf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return (void *)ileaf + btree->sb->blocksize;
}

static inline unsigned __atdict(__be16 *dict, unsigned at)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(at);
	return be16_to_cpu(*(dict - at));
}

static inline unsigned atdict(__be16 *dict, unsigned at)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return at ? __atdict(dict, at) : 0;
}

static inline void add_idict(__be16 *dict, int n)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	be16_add_cpu(dict, n);
}

static inline unsigned icount(struct ileaf *leaf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return be16_to_cpu(leaf->count);
}

static inline tuxkey_t ibase(struct ileaf *leaf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return be64_to_cpu(leaf->ibase);
}

static void ileaf_btree_init(struct btree *btree)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	btree->entries_per_leaf = 1 << (btree->sb->blockbits - 6);
}

static int ileaf_init(struct btree *btree, void *leaf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct ileaf_attr_ops *attr_ops = btree->ops->private_ops;
	trace("initialize inode leaf %p", leaf);
	*(struct ileaf *)leaf = (struct ileaf){
		.magic = attr_ops->magic,
	};
	return 0;
}

static int ileaf_need(struct btree *btree, struct ileaf *ileaf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	__be16 *dict = ileaf_dict(btree, ileaf);
	unsigned count = icount(ileaf);
	return atdict(dict, count) + count * sizeof(*dict);
}

static int ileaf_free(struct btree *btree, struct ileaf *ileaf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return btree->sb->blocksize
		- ileaf_need(btree, ileaf) - sizeof(struct ileaf);
}

static int ileaf_sniff(struct btree *btree, void *leaf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct ileaf_attr_ops *attr_ops = btree->ops->private_ops;
	return ((struct ileaf *)leaf)->magic == attr_ops->magic;
}

static int ileaf_can_free(struct btree *btree, void *leaf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct ileaf *ileaf = leaf;
	return icount(ileaf) == 0;
}

static void ileaf_dump(struct btree *btree, void *vleaf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	if (!tux3_trace)
		return;

	struct ileaf_attr_ops *attr_ops = btree->ops->private_ops;
	struct ileaf *leaf = vleaf;
	inum_t inum = ibase(leaf);
	__be16 *dict = ileaf_dict(btree, leaf);
	unsigned offset = 0;

	trace_on("ileaf 0x%Lx/%i (%x bytes free)",
		 ibase(leaf), icount(leaf), ileaf_free(btree, leaf));

	for (int i = 0; i < icount(leaf); i++, inum++) {
		int limit = __atdict(dict, i + 1), size = limit - offset;
		if (!size)
			continue;
		if (size < 0)
			trace_on("  0x%Lx: <corrupt>\n", inum);
		else if (!size)
			trace_on("  0x%Lx: <empty>\n", inum);
		else if (attr_ops == &iattr_ops) {
			/* FIXME: this doesn't work in kernel */
			struct tux3_inode tuxnode = {};
			struct inode *inode = &tuxnode.vfs_inode;
			void *attrs = leaf->table + offset;

			inode->i_sb = vfs_sb(btree->sb),
			attr_ops->decode(btree, inode, attrs, size);

			free_xcache(inode);
		}
		offset = limit;
	}
}

void *ileaf_lookup(struct btree *btree, inum_t inum, struct ileaf *leaf, unsigned *result)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(inum >= ibase(leaf));
	tuxkey_t at = inum - ibase(leaf), size = 0;
	void *attrs = NULL;

	trace("lookup inode 0x%Lx, %Lx + %Lx", inum, ibase(leaf), at);
	if (at < icount(leaf)) {
		__be16 *dict = ileaf_dict(btree, leaf);
		unsigned offset = atdict(dict, at);
		if ((size = __atdict(dict, at + 1) - offset))
			attrs = leaf->table + offset;
	}
	*result = size;
	return attrs;
}

static int isinorder(struct btree *btree, struct ileaf *leaf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	__be16 *dict = ileaf_dict(btree, leaf);

	for (int i = 0, offset = 0, limit; i < icount(leaf); i++, offset = limit)
		if ((limit = __atdict(dict, i + 1)) < offset)
			return 0;
	return 1;
}

/* userland only */
int ileaf_check(struct btree *btree, struct ileaf *leaf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct ileaf_attr_ops *attr_ops = btree->ops->private_ops;
	char *why;

	why = "not an ileaf";
	if (leaf->magic != attr_ops->magic)
		goto eek;
	why = "dict out of order";
	if (!isinorder(btree, leaf))
		goto eek;
	return 0;
eek:
	tux3_err(btree->sb, "%s!", why);
	return -1;
}

static void ileaf_trim(struct btree *btree, struct ileaf *leaf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	__be16 *dict = ileaf_dict(btree, leaf);
	unsigned count = icount(leaf);

	while (count > 1 && *(dict - count) == *(dict - count + 1))
		count--;
	if (count == 1 && !*(dict - 1))
		count = 0;
	leaf->count = cpu_to_be16(count);
}

#define SPLIT_AT_INUM

static tuxkey_t ileaf_split(struct btree *btree, tuxkey_t hint,
			    void *from, void *into)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(ileaf_sniff(btree, from));
	struct ileaf *leaf = from, *dest = into;
	__be16 *dict = ileaf_dict(btree, from);
	__be16 *destdict = ileaf_dict(btree, into);

#ifdef SPLIT_AT_INUM
	/*
	 * This is to prevent to have same ibase on both of from and into
	 * FIXME: we would want to split at better position.
	 */
	if (hint == ibase(leaf))
		hint++;

	trace("split at inum 0x%Lx", hint);
	unsigned at = min_t(tuxkey_t, hint - ibase(leaf), icount(leaf));
#else
	/* binsearch inum starting nearest middle of block */
	unsigned at = 1, hi = icount(leaf);
	while (at < hi) {
		int mid = (at + hi) / 2;
		if (*(dict - mid) < (btree->sb->blocksize / 2))
			at = mid + 1;
		else
			hi = mid;
	}
#endif
	/* should trim leading empty inodes on copy */
	unsigned split = atdict(dict, at), free = atdict(dict, icount(leaf));
	trace("split at %x of %x", at, icount(leaf));
	trace("copy out %x bytes at %x", free - split, split);
	assert(free >= split);
	memcpy(dest->table, leaf->table + split, free - split);
	dest->count = cpu_to_be16(icount(leaf) - at);
	veccopy(destdict - icount(dest), dict - icount(leaf), icount(dest));
	for (int i = 1; i <= icount(dest); i++)
		add_idict(destdict - i, -split);
#ifdef SPLIT_AT_INUM
	/* round down to multiple of 64 above ibase */
	inum_t round = hint & ~(inum_t)(btree->entries_per_leaf - 1);
	dest->ibase = cpu_to_be64(round > ibase(leaf) + icount(leaf) ? round : hint);
#else
	dest->ibase = cpu_to_be64(ibase(leaf) + at);
#endif
	leaf->count = cpu_to_be16(at);
	memset(leaf->table + split, 0, (char *)(dict - icount(leaf)) - (leaf->table + split));
	ileaf_trim(btree, leaf);
	return ibase(dest);
}

static int ileaf_merge(struct btree *btree, void *vinto, void *vfrom)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct ileaf *into = vinto, *from = vfrom;
	unsigned fromcount = icount(from);

	/* If "from" is empty, does nothing */
	if (!fromcount)
		return 1;

	assert(ibase(from) > ibase(into));
	tuxkey_t fromibase = ibase(from);
	unsigned count = icount(into);
	int hole = fromibase - ibase(into) + count;

	__be16 *dict = ileaf_dict(btree, into);
	__be16 *fromdict = ileaf_dict(btree, from);
	int need_size = hole * sizeof(*dict) + ileaf_need(btree, from);

	if (ileaf_free(btree, into) < need_size)
		return 0;

	/* Fill hole of dict until from_ibase */
	unsigned limit = atdict(dict, count);
	__be16 __limit = cpu_to_be16(limit);
	while (hole--) {
		count++;
		*(dict - count) = __limit;
	}

	/* Copy data from "from" */
	unsigned fromlimit = atdict(fromdict, fromcount);
	memcpy(into->table + limit, from->table, fromlimit);

	/* Adjust copying fromdict */
	if (limit) {
		int i;
		for (i = 1; i <= fromcount; i++)
			add_idict(dict - i, limit);
	}
	veccopy(dict - count - fromcount, fromdict - fromcount, fromcount);

	into->count = cpu_to_be16(count + fromcount);

	return 1;
}

/*
 * Chop inums
 * return value:
 * < 0 - error
 *   1 - modified
 *   0 - not modified
 */
static int ileaf_chop(struct btree *btree, tuxkey_t start, u64 len, void *leaf)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct ileaf *ileaf = leaf;
	__be16 *dict = ileaf_dict(btree, leaf);
	tuxkey_t base = ibase(ileaf);
	unsigned count = icount(ileaf);
	tuxkey_t at = start - base;
	void *startp, *endp, *tailp;
	unsigned size;

	if (at + 1 > count)
		return 0;

	len = min_t(u64, len, count - at);

	startp = ileaf->table + atdict(dict, at);
	endp = ileaf->table + atdict(dict, at + len);
	if (startp == endp)
		return 0;

	/* Remove data */
	tailp = ileaf->table + atdict(dict, count);
	memmove(startp, endp, tailp - endp);

	/* Adjust dict */
	size = endp - startp;
	while (at < count) {
		at++;
		add_idict(dict - at, -size);
	}

	ileaf_trim(btree, leaf);

	return 1;
}

static void *ileaf_resize(struct btree *btree, tuxkey_t inum, void *vleaf,
			  int newsize)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct ileaf *ileaf = vleaf;
	__be16 *dict = ileaf_dict(btree, ileaf);
	unsigned count = icount(ileaf);
	tuxkey_t at = inum - ibase(ileaf);
	int extend_dict, offset, size;

	assert(inum >= ibase(ileaf));

	/* Get existent attributes, and calculate expand/shrink size */
	if (at + 1 > count) {
		/* Check size roughly to avoid overflow int */
		if ((at + 1) * sizeof(*dict) >= btree->sb->blocksize)
			goto overflow;

		/* Need to extend dict */
		extend_dict = (at + 1 - count) * sizeof(*dict);
		offset = atdict(dict, count);
		size = 0;
	} else {
		/* "at" is in dict, so get attr size */
		extend_dict = 0;
		offset = atdict(dict, at);
		size = __atdict(dict, at + 1) - offset;
	}

	if (ileaf_free(btree, ileaf) < newsize - size + extend_dict) {
overflow:
		return NULL;
	}

	/* Extend dict */
	if (extend_dict) {
		__be16 limit = cpu_to_be16(atdict(dict, count));
		while (count < at + 1) {
			count++;
			*(dict - count) = limit;
		}
		ileaf->count = cpu_to_be16(count);
	}

	void *attrs = ileaf->table + offset;
	if (newsize != size) {
		/* Expand/Shrink attr space */
		unsigned limit = __atdict(dict, count);
		assert(limit >= offset + size);
		memmove(attrs + newsize, attrs + size, limit - offset - size);

		/* Adjust dict */
		int diff = newsize - size;
		at++;
		while (at <= count) {
			add_idict(dict - at, diff);
			at++;
		}
	}

	return attrs;
}

static tuxkey_t ileaf_split_hint(struct btree *btree, struct ileaf *ileaf,
				 tuxkey_t key, int size)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/*
	 * FIXME: make sure there is space for size.
	 * FIXME: better split position?
	 */

	tuxkey_t base = ibase(ileaf);
	unsigned count = icount(ileaf);
	if (key >= base + count)
		return key & ~(btree->entries_per_leaf - 1);

	return base + count / 2;
}

/*
 * Write inode attributes.
 */
static int ileaf_write(struct btree *btree, tuxkey_t key_bottom,
		       tuxkey_t key_limit,
		       void *leaf, struct btree_key_range *key,
		       tuxkey_t *split_hint)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct ileaf_req *rq = container_of(key, struct ileaf_req, key);
	struct ileaf_attr_ops *attr_ops = btree->ops->private_ops;
	struct ileaf *ileaf = leaf;
	void *attrs;
	int size;

	assert(key->len == 1);

	size = attr_ops->encoded_size(btree, rq->data);
	assert(size);

	attrs = ileaf_resize(btree, key->start, ileaf, size);
	if (attrs == NULL) {
		/* There is no space to store */
		*split_hint = ileaf_split_hint(btree, ileaf, key->start, size);
		return 1;	/* need to split */
	}

	attr_ops->encode(btree, rq->data, attrs, size);

	key->start++;
	key->len--;

	return 0;
}

static int ileaf_read(struct btree *btree, tuxkey_t key_bottom,
		      tuxkey_t key_limit,
		      void *leaf, struct btree_key_range *key)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct ileaf_req *rq = container_of(key, struct ileaf_req, key);
	struct ileaf_attr_ops *attr_ops = btree->ops->private_ops;
	struct ileaf *ileaf = leaf;
	void *attrs;
	unsigned size;

	attrs = ileaf_lookup(btree, key->start, ileaf, &size);
	if (attrs == NULL)
		return -ENOENT;

	return attr_ops->decode(btree, rq->data, attrs, size);
}

struct btree_ops itree_ops = {
	.btree_init	= ileaf_btree_init,
	.leaf_init	= ileaf_init,
	.leaf_split	= ileaf_split,
	.leaf_merge	= ileaf_merge,
	.leaf_chop	= ileaf_chop,
	.leaf_write	= ileaf_write,
	.leaf_read	= ileaf_read,
	.balloc		= balloc,
	.private_ops	= &iattr_ops,

	.leaf_sniff	= ileaf_sniff,
	.leaf_can_free	= ileaf_can_free,
	.leaf_dump	= ileaf_dump,
};

struct btree_ops otree_ops = {
	.btree_init	= ileaf_btree_init,
	.leaf_init	= ileaf_init,
	.leaf_split	= ileaf_split,
	.leaf_merge	= ileaf_merge,
	.leaf_chop	= ileaf_chop,
	.leaf_write	= ileaf_write,
	.leaf_read	= ileaf_read,
	.balloc		= balloc,
	.private_ops	= &oattr_ops,

	.leaf_sniff	= ileaf_sniff,
	.leaf_can_free	= ileaf_can_free,
	.leaf_dump	= ileaf_dump,
};

/*
 * Find free inum
 * (callback for btree_traverse())
 *
 * return value:
 * 1 - found
 * 0 - not found
 */
int ileaf_find_free(struct btree *btree, tuxkey_t key_bottom,
		    tuxkey_t key_limit, void *leaf,
		    tuxkey_t key, u64 len, void *data)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	tuxkey_t at = key - ibase(leaf);
	unsigned count = icount(leaf);

	key_limit = min(key_limit, key + len);

	if (at < count) {
		__be16 *dict = ileaf_dict(btree, leaf);
		unsigned limit, offset = atdict(dict, at);

		while (at < count) {
			at++;
			limit = __atdict(dict, at);
			if (offset == limit) {
				at--;
				break;
			}
			offset = limit;
		}
	}

	if (ibase(leaf) + at < key_limit) {
		*(inum_t *)data = ibase(leaf) + at;
		return 1;
	}

	return 0;
}

/*
 * Enumerate inum
 * (callback for btree_traverse())
 */
int ileaf_enumerate(struct btree *btree, tuxkey_t key_bottom,
		    tuxkey_t key_limit, void *leaf,
		    tuxkey_t key, u64 len, void *data)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct ileaf *ileaf = leaf;
	__be16 *dict = ileaf_dict(btree, ileaf);
	struct ileaf_enumrate_cb *cb = data;
	tuxkey_t at, base = ibase(ileaf);
	unsigned count;

	at = key - base;
	count = min_t(u64, key + len - base, icount(ileaf));

	if (at < count) {
		unsigned offset = atdict(dict, at);
		for (; at < count; at++) {
			unsigned size, limit;
			inum_t inum;
			void *attrs;
			int err;

			limit = __atdict(dict, at + 1);
			if (limit <= offset)
				continue;
			attrs = ileaf->table + offset;
			size = limit - offset;

			inum = base + at;
			err = cb->callback(btree, inum, attrs, size, cb->data);
			if (err)
				return err;

			offset = limit;
		}
	}

	return 0;
}
