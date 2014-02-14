#include <tux3user.h>

/* depending on tux3 */

void inc_nlink(struct inode *inode)
{
	if(DEBUG_MODE_L==1)
	{
		printf("\t\t\t\t%25s[L]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	inode->i_nlink++;
}

void drop_nlink(struct inode *inode)
{
	if(DEBUG_MODE_L==1)
	{
		printf("\t\t\t\t%25s[L]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(inode->i_nlink > 0);
	inode->i_nlink--;
}

void clear_nlink(struct inode *inode)
{
	if(DEBUG_MODE_L==1)
	{
		printf("\t\t\t\t%25s[L]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	inode->i_nlink = 0;
}

void set_nlink(struct inode *inode, unsigned int nlink)
{
	if(DEBUG_MODE_L==1)
	{
		printf("\t\t\t\t%25s[L]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	if (!nlink)
		clear_nlink(inode);
	else
		inode->i_nlink = nlink;
}

void d_instantiate(struct dentry *dentry, struct inode *inode)
{
	if(DEBUG_MODE_L==1)
	{
		printf("\t\t\t\t%25s[L]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	dentry->d_inode = inode;
}

struct dentry *d_splice_alias(struct inode *inode, struct dentry *dentry)
{
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	d_instantiate(dentry, inode);
	return NULL;
}

void truncate_pagecache(struct inode *inode, loff_t oldsize, loff_t newsize)
{
	if(DEBUG_MODE_L==1)
	{
		printf("\t\t\t\t%25s[L]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	truncate_inode_pages(mapping(inode), newsize);
}

void truncate_setsize(struct inode *inode, loff_t newsize)
{
	if(DEBUG_MODE_L==1)
	{
		printf("\t\t\t\t%25s[L]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	loff_t oldsize = inode->i_size;

	inode->i_size = newsize;
	if (newsize < oldsize)
		truncate_pagecache(inode, oldsize, newsize);
}
