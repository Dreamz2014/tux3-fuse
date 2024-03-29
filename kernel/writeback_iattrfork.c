/*
 * Iattr  Fork (Copy-On-Write of inode attributes)
 *
 * Iattr fork is to reduce copy, it copies inode attributes once at most
 * per delta.
 *
 * If iattrs was dirtied by previous delta and stabled, the frontend
 * copy iattrs to backend slot from frontend slot before modify
 * iattrs, and remember delta number when dirtied.
 *
 * The backend checks the delta number of iattr. If delta number of
 * iattr == backend delta, the frontend didn't modify iattrs after
 * stabled, so backend uses the frontend slot. Then backend clears
 * delta number of iattr to tell the backend doesn't need fork
 * anymore.
 *
 * Otherwise, frontend forked the iatts to backend slot, so backend
 * uses the backend slot.
 */


#include "tux3_fork.h"
#include "iattr.h"

TUX3_DEFINE_STATE_FNS(unsigned, iattr, IATTR_DIRTY,
		      IFLAGS_IATTR_BITS, IFLAGS_IATTR_SHIFT);

/* FIXME: can we consolidate tuxnode->lock usage with I_DIRTY and xattrdirty? */
/* FIXME: timestamps is updated without i_mutex, so racy. */

/* Caller must hold tuxnode->lock. */
static void idata_copy(struct inode *inode, struct tux3_iattr_data *idata)
{
	idata->present		= tux_inode(inode)->present;
	idata->i_mode		= inode->i_mode;
	idata->i_uid		= i_uid_read(inode);
	idata->i_gid		= i_gid_read(inode);
	idata->i_nlink		= inode->i_nlink;
	idata->i_rdev		= inode->i_rdev;
	idata->i_size		= inode->i_size;
//	idata->i_atime		= inode->i_atime;
	idata->i_mtime		= inode->i_mtime;
	idata->i_ctime		= inode->i_ctime;
	idata->i_version	= inode->i_version;
}

void tux3_iattrdirty(struct inode *inode)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct tux3_inode *tuxnode = tux_inode(inode);
	unsigned delta = tux3_inode_delta(inode);
	unsigned flags = tuxnode->flags;

	/* If dirtied on this delta, nothing to do */
	if (tux3_iattrsta_has_delta(flags) &&
	    tux3_iattrsta_get_delta(flags) == tux3_delta(delta))
		return;

	trace("inum %Lu, delta %u", tuxnode->inum, delta);

	spin_lock(&tuxnode->lock);
	flags = tuxnode->flags;
	if (S_ISREG(inode->i_mode) || tux3_iattrsta_has_delta(flags)) {
		unsigned old_delta;

		/*
		 * For a regular file, and even if iattrs are clean,
		 * we have to provide stable idata for backend.
		 *
		 * Because backend may be committing data pages. If
		 * so, backend have to check idata->i_size, and may
		 * save dtree root. But previous delta doesn't have
		 * stable iattrs.
		 *
		 * So, this provides stable iattrs for regular file,
		 * even if previous delta is clean.
		 *
		 * Other types don't have this problem, because:
		 * - Never dirty iattr (e.g. volmap). IOW, iattrs are
		 *   always stable.
		 * - Or dirty iattr with data, e.g. directory updates
		 *   timestamp too with data blocks.
		 */
		if (S_ISREG(inode->i_mode) && !tux3_iattrsta_has_delta(flags))
			old_delta = tux3_delta(delta - 1);
		else
			old_delta = tux3_iattrsta_get_delta(flags);

		/* If delta is difference, iattrs was stabilized. Copy. */
		if (old_delta != tux3_delta(delta)) {
			struct tux3_iattr_data *idata =
				&tux3_inode_ddc(inode, old_delta)->idata;
			idata_copy(inode, idata);
		}
	}
	/* Update iattr state to current delta */
	tuxnode->flags = tux3_iattrsta_update(flags, delta);
	spin_unlock(&tuxnode->lock);
}

/* Caller must hold tuxnode->lock */
static void tux3_iattr_clear_dirty(struct tux3_inode *tuxnode)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	trace("inum %Lu", tuxnode->inum);
	tuxnode->flags = tux3_iattrsta_clear(tuxnode->flags);
}

/*
 * Read iattrs, then clear iattr dirty to tell no need to iattrfork
 * anymore if needed.
 *
 * Caller must hold tuxnode->lock.
 */
static void tux3_iattr_read_and_clear(struct inode *inode,
				      struct tux3_iattr_data *result,
				      unsigned delta)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct tux3_inode *tuxnode = tux_inode(inode);
	unsigned long flags;

	trace("inum %Lu, delta %u", tuxnode->inum, delta);

	/*
	 * If delta is same, iattrs are available in inode. If not,
	 * iattrs were forked.
	 */
	flags = tuxnode->flags;
	if (!tux3_iattrsta_has_delta(flags) ||
	    tux3_iattrsta_get_delta(flags) == tux3_delta(delta)) {
		/*
		 * If btree is only dirtied, or if dirty and no fork,
		 * use inode.
		 */
		idata_copy(inode, result);
		tuxnode->flags = tux3_iattrsta_clear(flags);
	} else {
		/* If dirty and forked, use copy */
		struct tux3_iattr_data *idata =
			&tux3_inode_ddc(inode, delta)->idata;
		assert(idata->present != TUX3_INVALID_PRESENT);
		*result = *idata;
	}

	/* For debugging, set invalid value to ->present after read */
	tux3_inode_ddc(inode, delta)->idata.present = TUX3_INVALID_PRESENT;
}

/*
 * DATA_BTREE_BIT is not set in normal state. We set it only when
 * flush inode.  So, this is called to flush inode.
 */
static void tux3_iattr_adjust_for_btree(struct inode *inode,
					struct tux3_iattr_data *idata)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	if (has_root(&tux_inode(inode)->btree))
		idata->present |= DATA_BTREE_BIT;
}
