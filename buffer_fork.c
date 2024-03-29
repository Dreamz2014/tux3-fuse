/*
 * Block Fork (Copy-On-Write of logically addressed block)
 */

/*
 * For now, there is no concurrent reader in the userland, so we can
 * free the buffer at I/O completion.
 */
void free_forked_buffers(struct sb *sb, struct inode *inode, int force)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	
}

struct buffer_head *blockdirty(struct buffer_head *buffer, unsigned newdelta)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	map_t *map = buffer->map;

	assert(buffer->state < BUFFER_STATES);

	buftrace("---- before: fork buffer %p ----", buffer);
	if (buffer_dirty(buffer)) {
		if (buffer_already_dirty(buffer, newdelta))
			return buffer;

		/* Buffer can't modify already, we have to fork buffer */
		buftrace("---- fork buffer %p ----", buffer);
		struct buffer_head *clone = new_buffer(map);
		if (IS_ERR(clone))
			return clone;
		/* Create the cloned buffer */
		memcpy(bufdata(clone), bufdata(buffer), bufsize(buffer));
		clone->index = buffer->index;
		/* Replace the buffer by cloned buffer. */
		remove_buffer_hash(buffer);
		insert_buffer_hash(clone);

		/*
		 * The refcount of buffer is used for backend. So, the
		 * backend has to free this buffer (blockput(buffer))
		 */
		buffer = clone;
	}

	__tux3_mark_buffer_dirty(buffer, newdelta);

	return buffer;
}

int bufferfork_to_invalidate(map_t *map, struct buffer_head *buffer)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned delta = tux3_inode_delta(map->inode);
	/*
	 * The userland shouldn't need to buffer fork on truncate
	 * path, because no async backend.  So, just make sure it.
	 */
	assert(buffer_can_modify(buffer, delta));

	return 0;
}
