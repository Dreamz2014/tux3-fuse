/*
 * Write back buffers
 */
extern int compress_stride(struct bufvec*);
/*
 * Helper for waiting I/O (stub)
 */

void tux3_iowait_init(struct iowait *iowait)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	
}

void tux3_iowait_wait(struct iowait *iowait)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	
}

/*
 * Helper for buffer vector I/O.
 */

#define buffers_entry(x) \
	list_entry(x, struct buffer_head, link)
#define MAX_BUFVEC_COUNT	UINT_MAX

/* Initialize bufvec */
void bufvec_init(struct bufvec *bufvec, map_t *map,
		 struct list_head *head, struct tux3_iattr_data *idata)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	INIT_LIST_HEAD(&bufvec->contig);
	INIT_LIST_HEAD(&bufvec->compress);
	INIT_LIST_HEAD(&bufvec->for_io);
	bufvec->global_index = 0;
	bufvec->buffers		= head;
	bufvec->contig_count	= 0;
	bufvec->compress_count  = 0;
	bufvec->idata		= idata;
	bufvec->map		= map;
	bufvec->end_io		= NULL;
	
	/*
	printf("\n----  INODE : %Lu  ----\n",tux_inode(map->inode)->inum);
	printf("\nFRONTEND ? DIRTY BUFFERS\n");
	show_dirty_buffers(map);
	printf("\nEND\n");

	printf("\nSHOW_ALL_BUFFERS\n");
	show_buffers(map);
	printf("\n-----END-----\n");
	*/
}

void bufvec_free(struct bufvec *bufvec)
{
	/* FIXME: on error path, this will happens */
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(!bufvec->buffers || list_empty(bufvec->buffers));
	assert(list_empty(&bufvec->contig));
	assert(list_empty(&bufvec->for_io));
}

static inline void bufvec_buffer_move_to_contig(struct bufvec *bufvec,
						struct buffer_head *buffer)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	list_move_tail(&buffer->link, &bufvec->contig);
	bufvec->contig_count++;
}

static void bufvec_io_done(struct bufvec *bufvec, int err)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct list_head *head = &bufvec->for_io;

	while (!list_empty(head)) {
		struct buffer_head *buffer = buffers_entry(head->next);
		list_del_init(&buffer->link);
		bufvec->end_io(buffer, err);
	}
	if(is_compressed_file(bufvec_inode(bufvec)))
	{
		head = &bufvec->compress;
		while (!list_empty(head)) {
			struct buffer_head *buffer = buffers_entry(head->next);
			list_del_init(&buffer->link);
			bufvec->end_io(buffer, err);
		}
	bufvec->compress_count = 0;
	}
}

/* Get the next candidate buffer. */
static struct buffer_head *bufvec_next_buffer(struct bufvec *bufvec)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	if (!list_empty(&bufvec->contig))
		return bufvec_contig_buf(bufvec);

	if (bufvec->buffers && !list_empty(bufvec->buffers))
		return buffers_entry(bufvec->buffers->next);

	return NULL;
}

/*
 * Prepare and submit I/O for specified range.
 *
 * This doesn't guarantee all candidate buffers are prepared for
 * I/O. It might be limited by device or block layer.
 *
 * return value:
 * < 0 - error
 *   0 - success
 */
int bufvec_io(int rw, struct bufvec *bufvec, block_t physical, unsigned count)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct sb *sb = tux_sb(bufvec_inode(bufvec)->i_sb);
	struct iovec *iov;
	unsigned i, iov_count;
	int err;

	assert(count <= bufvec_contig_count(bufvec));

	iov = malloc(sizeof(*iov) * count);
	if (iov == NULL)
		return -ENOMEM;
	iov_count = 0;

	/* Add buffers for I/O */
	for (i = 0; i < count; i++) {
		struct buffer_head *buffer = bufvec_contig_buf(bufvec);

		/* buffer will be re-added into per-state list after I/O done */
		list_move_tail(&buffer->link, &bufvec->for_io);
		bufvec->contig_count--;

		iov[i].iov_base = bufdata(buffer);
		iov[i].iov_len = bufsize(buffer);
		iov_count++;
	}
	assert(i > 0);

	err = devio_vec(rw, sb_dev(sb), physical << sb->blockbits,
			iov, iov_count);
	bufvec_io_done(bufvec, err);

	free(iov);

	return 0;
}

/*
 * Call completion without I/O. I.e. change buffer state without I/O.
 */
void bufvec_complete_without_io(struct bufvec *bufvec, unsigned count)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned i;

	assert(count <= bufvec_contig_count(bufvec));

	/* Add buffers for completion */
	for (i = 0; i < count; i++) {
		struct buffer_head *buffer = bufvec_contig_buf(bufvec);

		/* buffer will be re-added into per-state list after I/O done */
		list_move_tail(&buffer->link, &bufvec->for_io);
		bufvec->contig_count--;
	}
	assert(i > 0);

	bufvec_io_done(bufvec, 0);
}

/*
 * Try to add buffer to bufvec as contiguous range.
 *
 * return value:
 * 1 - success
 * 0 - fail to add
 */
int bufvec_contig_add(struct bufvec *bufvec, struct buffer_head *buffer)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned contig_count = bufvec_contig_count(bufvec);

	if (contig_count) {
		block_t last;

		/* Check contig_count limit */
		if (bufvec_contig_count(bufvec) == MAX_BUFVEC_COUNT)
			return 0;

		/* Check if buffer is logically contiguous */
		last = bufvec_contig_last_index(bufvec);
		if (last != bufindex(buffer) - 1)
			return 0;
	}

	bufvec_buffer_move_to_contig(bufvec, buffer);

	return 1;
}

static void cancel_buffer_dirty(struct bufvec *bufvec,
				struct buffer_head *buffer)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	if (tux_inode(bufvec_inode(bufvec))->inum == TUX_VOLMAP_INO)
		__clear_buffer_dirty_for_endio(buffer, 0);
	else
		clear_buffer_dirty_for_endio(buffer, 0);
}

/* Cancel dirty buffers fully outside i_size */
static void bufvec_cancel_dirty_outside(struct bufvec *bufvec)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct buffer_head *buffer;

	while (!list_empty(bufvec->buffers)) {
		buffer = buffers_entry(bufvec->buffers->next);
		buftrace("cancel dirty: buffer %p, block %Lu",
			 buffer, bufindex(buffer));

		list_del_init(&buffer->link);
		/* Cancel buffer dirty of outside i_size */
		cancel_buffer_dirty(bufvec, buffer);
	}
}

/*
 * Try to collect logically contiguous dirty range from bufvec->buffers.
 *
 * return value:
 * 1 - there is buffers for I/O
 * 0 - no buffers for I/O
 */
static int bufvec_contig_collect(struct bufvec *bufvec)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct sb *sb = tux_sb(bufvec_inode(bufvec)->i_sb);
	struct tux3_iattr_data *idata = bufvec->idata;
	struct buffer_head *buffer;
	block_t last_index, next_index, outside_block;

	/* If there is in-progress contiguous range, leave as is */
	if (bufvec_contig_count(bufvec))
		return 1;
	assert(!list_empty(bufvec->buffers));

	outside_block = (idata->i_size + sb->blockmask) >> sb->blockbits;

	buffer = buffers_entry(bufvec->buffers->next);
	next_index = bufindex(buffer);
	/* If next buffer is fully outside i_size, clear dirty */
	if (next_index >= outside_block) {
		bufvec_cancel_dirty_outside(bufvec);
		return 0;
	}

	do {
		/* Check contig_count limit */
		if (bufvec_contig_count(bufvec) == (is_compressed_file(bufvec_inode(bufvec))?COMPRESSION_STRIDE_LEN:UINT_MAX))
			break;
		bufvec_buffer_move_to_contig(bufvec, buffer);

		if (list_empty(bufvec->buffers))
			break;

		buffer = buffers_entry(bufvec->buffers->next);
		last_index = next_index;
		next_index = bufindex(buffer);
/*		if(tux_inode(bufvec_inode(bufvec))->inum >= 64)
		{
			int i;
			printf("DO YOU WANT TO COMPRESS?");
			scanf("%d",&i);
			if(i==1)
			{
				(tux_inode(bufvec_inode(bufvec)))->is_compressed=1;
				tux3_mark_inode_dirty(bufvec_inode(bufvec));
				//iput(bufvec_inode(bufvec));
			}
		}
*/
	/* If next buffer is fully outside i_size, clear dirty */
		if (next_index >= outside_block) {
			bufvec_cancel_dirty_outside(bufvec);
			break;
		}
	} while (last_index == next_index - 1);
	
		
	if(tux_inode(bufvec_inode(bufvec))->inum >= 64 && is_compressed_file(bufvec_inode(bufvec)))
	        compress_stride(bufvec);

        return !!bufvec_contig_count(bufvec);
}

static int buffer_index_cmp(void *priv, struct list_head *a,
			    struct list_head *b)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct buffer_head *buf_a = list_entry(a, struct buffer_head, link);
	struct buffer_head *buf_b = list_entry(b, struct buffer_head, link);

	if (bufindex(buf_a) < bufindex(buf_b))
		return -1;
	else if (bufindex(buf_a) > bufindex(buf_b))
		return 1;
	return 0;
}

/*
 * Flush buffers in head
 */
int flush_list(map_t *map, struct tux3_iattr_data *idata,
	       struct list_head *head)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct bufvec bufvec;
	int err = 0;

	/* FIXME: on error path, we have to do something for buffer state */

	if (list_empty(head))
		return 0;

	bufvec_init(&bufvec, map, head, idata);

	/* Sort by bufindex() */
	list_sort(NULL, head, buffer_index_cmp);

	while (bufvec_next_buffer(&bufvec)) {
		/* Collect contiguous buffer range */
		if (bufvec_contig_collect(&bufvec)) {
			/* Start I/O */
			err = map->io(WRITE, &bufvec);
			if (err)
				break;
		}
	}

	bufvec_free(&bufvec);

	return err;
}
