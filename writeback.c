#include "tux3user.h"

#ifndef trace
#define trace trace_on
#endif

#include "kernel/writeback.c"

void clear_inode(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	inode->i_state = I_FREEING;
}

void __mark_inode_dirty(struct inode *inode, unsigned flags)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	if (flags & (I_DIRTY_SYNC | I_DIRTY_DATASYNC))
		tux3_dirty_inode(inode, flags);

	if ((inode->i_state & flags) != flags)
		inode->i_state |= flags;
}

void mark_inode_dirty(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	__mark_inode_dirty(inode, I_DIRTY);
}

void mark_inode_dirty_sync(struct inode *inode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	__mark_inode_dirty(inode, I_DIRTY_SYNC);
}

int sync_super(struct sb *sb)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return force_delta(sb);
}
