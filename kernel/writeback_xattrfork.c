/*
 * Xcache  Fork (Copy-On-Write of extended attributes)
 */


void tux3_xattrdirty(struct inode *inode)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
}

void tux3_xattr_read_and_clear(struct inode *inode)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
}
