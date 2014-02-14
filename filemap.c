#include "tux3user.h"

#ifndef trace
#define trace trace_on
#endif
#include "kernel/filemap.c"
#include "compression.c"

static int filemap_bufvec_check(struct bufvec *bufvec, enum map_mode mode)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct sb *sb = tux_sb(bufvec_inode(bufvec)->i_sb);
	struct buffer_head *buffer;

	trace("%s inode 0x%Lx block 0x%Lx",
	      (mode == MAP_READ) ? "read" :
			(mode == MAP_WRITE) ? "write" : "redirect",
	      tux_inode(bufvec_inode(bufvec))->inum,
	      bufvec_contig_index(bufvec));

	if (bufvec_contig_last_index(bufvec) & (-1LL << MAX_BLOCKS_BITS))
	{
		if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return -EIO;
	}

	list_for_each_entry(buffer, &bufvec->contig, link) {
		if (mode != MAP_READ && buffer_empty(buffer))
			tux3_warn(sb, "egad, writing an invalid buffer");
		if (mode == MAP_READ && buffer_dirty(buffer))
			tux3_warn(sb, "egad, reading a dirty buffer");
	}

	if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return 0;
}

/*
 * Extrapolate from single buffer blockread to opportunistic extent IO
 *
 * Essentially readahead:
 *  - stop at first present buffer
 *  - stop at end of file
 *
 * Stop when extent is "big enough", whatever that means.
 */
static int guess_readahead(struct bufvec *bufvec, struct inode *inode,
			   block_t index)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct sb *sb = inode->i_sb;
	struct buffer_head *buffer;
	block_t limit;
	int ret;

	bufvec_init(bufvec, inode->map, NULL, NULL);

	limit = (inode->i_size + sb->blockmask) >> sb->blockbits;
	/* FIXME: MAX_EXTENT is not true for dleaf2 */
	if (limit > index + MAX_EXTENT)
		limit = index + MAX_EXTENT;

	/*
	 * FIXME: pin buffers early may be inefficient. We can delay to
	 * prepare buffers until map_region() was done.
	 */
	buffer = blockget(mapping(inode), index++);
	if (!buffer)
	{
		if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return -ENOMEM;		/* FIXME: error code */
	}
	ret = bufvec_contig_add(bufvec, buffer);
	assert(ret);

	while (index < limit) {
		struct buffer_head *nextbuf = peekblk(buffer->map, index);
		if (nextbuf) {
			unsigned stop = !buffer_empty(nextbuf);
			if (stop) {
				blockput(nextbuf);
				break;
			}
		} else {
			nextbuf = blockget(buffer->map, index);
			if (!nextbuf)
				break;
		}
		ret = bufvec_contig_add(bufvec, nextbuf);
		assert(ret);

		index++;
	}

	if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return 0;
}

/* For read end I/O */
static void filemap_read_endio(struct buffer_head *buffer, int err)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	if (err) {
		/* FIXME: What to do? Hack: This re-link to state from bufvec */
		assert(0);
		__set_buffer_empty(buffer);
	} else {
		set_buffer_clean(buffer);
	}
	/* This drops refcount for bufvec of guess_readahead() */
	blockput(buffer);
	if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);}
}


/* For hole region */
static void filemap_hole_endio(struct buffer_head *buffer, int err)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(err == 0);
	memset(bufdata(buffer), 0, bufsize(buffer));
	set_buffer_clean(buffer);
	/* This drops refcount for bufvec of guess_readahead() */
	blockput(buffer);
	if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);}
}


/* For readahead cleanup */
static void filemap_clean_endio(struct buffer_head *buffer, int err)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	assert(err == 0);
	__set_buffer_empty(buffer);
	/* This drops refcount for bufvec of guess_readahead() */
	blockput(buffer);
	if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);}
}


static int filemap_extent_io(enum map_mode mode, int rw, struct bufvec *bufvec)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct inode *inode = bufvec_inode(bufvec);
	block_t block, index = bufvec_contig_index(bufvec);
	int err;

	/* FIXME: now assuming buffer is only 1 for MAP_READ */
	assert(mode != MAP_READ || bufvec_contig_count(bufvec) == 1);
	err = filemap_bufvec_check(bufvec, mode);
	if (err)
	{
		if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return err;
	}

	struct bufvec *bufvec_io, bufvec_ahead;
	unsigned count;
	if (!(rw & WRITE)) {
		/* In the case of read, use new bufvec for readahead */
		err = guess_readahead(&bufvec_ahead, inode, index);
		if (err)
		{
			if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return err;
		}
		bufvec_io = &bufvec_ahead;
	} else {
		bufvec_io = bufvec;
	}
	count = bufvec_contig_count(bufvec_io);

	struct block_segment seg[10];

	int segs = map_region(inode, index, count, seg, ARRAY_SIZE(seg), mode);
	if (segs < 0)
	{
		if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return segs;
	}
	assert(segs);

	for (int i = 0; i < segs; i++) {
		block = seg[i].block;
		count = seg[i].count;

		trace("extent 0x%Lx/%x => %Lx", index, count, block);

		if (seg[i].state != BLOCK_SEG_HOLE) {
			if (!(rw & WRITE))
				bufvec_io->end_io = filemap_read_endio;
			else
				bufvec_io->end_io = clear_buffer_dirty_for_endio;

			err = blockio_vec(rw, bufvec_io, block, count);
			if (err)
				break;
		} else {
			assert(!(rw & WRITE));
			bufvec_io->end_io = filemap_hole_endio;
			bufvec_complete_without_io(bufvec_io, count);
		}

		index += count;
	}

	/*
	 * In the write case, bufvec owner is caller. And caller must
	 * be handle buffers was not mapped (and is not written out)
	 * this time.
	 */
	if (!(rw & WRITE)) {
		/* Clean buffers was not mapped in this time */
		count = bufvec_contig_count(bufvec_io);
		if (count) {
			bufvec_io->end_io = filemap_clean_endio;
			bufvec_complete_without_io(bufvec_io, count);
		}
		bufvec_free(bufvec_io);
	}

	if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return err;
}

static int tuxio(struct file *file, void *data, unsigned len, int write)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	unsigned delta = write ? tux3_get_current_delta() : 0;
	struct inode *inode = file->f_inode;
	struct sb *sb = tux_sb(inode->i_sb);
	loff_t pos = file->f_pos;
	int err = 0;
	/*AplaCode*/
	//int len=inode->i_size;
	lzo_uint stride_len;
	char *clone1[COMPRESSION_STRIDE_LEN];
	unsigned char *compressed_data;
	unsigned decompressed_length;
	int i,r;
	unsigned len2=len;
	int stride_count=1;
	int blocks_count=0;
	init_stride();
	/*if(!write)
	{
		printf("\n$$!Full length : %u, inode full length : %Ld, inode compressed length : %Ld\n",len,inode->i_size,(tux_inode(inode))->i_size_compr);
		len=(tux_inode(inode))->i_size_compr;
	}*/
	/*AplaCode*/
	trace("%s %u bytes at %Lu, isize = 0x%Lx",
	      write ? "write" : "read", len, (s64)pos, (s64)inode->i_size);
	if (write && pos + len > sb->s_maxbytes)
	{
		if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return -EFBIG;
	}
	if (!write && pos + len > inode->i_size) {
		if (pos >= inode->i_size)
		{
			if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return 0;
		}
		len = inode->i_size - pos;
	}

	if (write) {
		tux3_iattrdirty(inode);
		inode->i_mtime = inode->i_ctime = gettime();
	}

	unsigned bbits = sb->blockbits;
	unsigned bsize = sb->blocksize;
	unsigned bmask = sb->blockmask;

	loff_t tail = len;
	while (tail) {
		struct buffer_head *buffer, *clone;
		unsigned from = pos & bmask;
		unsigned some = from + tail > bsize ? bsize - from : tail;
		int full = write && some == bsize;

		if (full)
			buffer = blockget(mapping(inode), pos >> bbits);
		else
			buffer = blockread(mapping(inode), pos >> bbits);
		if (!buffer) {
			err = -EIO;
			break;
		}
		if(!write)
		{
		}

		if (write) {
			clone = blockdirty(buffer, delta);
			if (IS_ERR(clone)) {
				blockput(buffer);
				err = PTR_ERR(clone);
				break;
			}

			memcpy(bufdata(clone) + from, data, some);
			mark_buffer_dirty_non(clone);
		} 
		else 
		{
			clone=buffer;
			//assert(is_stride_initialised());
			if(is_compressed_file(inode))
			{
				clone1[blocks_count]=malloc(PAGE_SIZE_1);
				memcpy(clone1[blocks_count],buffer->data,PAGE_SIZE_1);
				printf("\nstride Count : %d ; count : %d\n",stride_count,blocks_count);
				blocks_count++;
				
				if(blocks_count==(em.num[stride_count]))
				{
					compressed_data=malloc(PAGE_SIZE_1*COMPRESSION_STRIDE_LEN);
					for(i=0;i<em.num[stride_count];i++)
					{
						memcpy(compressed_data+i*PAGE_SIZE_1,(void *)clone1[i], some);
						//decompressed_data=decompressed_data+PAGE_SIZE_1;	
						free(clone1[i]);
					}
					
					printf("\n-----------------------------------------------------------------------\n");
					for(i=0;i<PAGE_SIZE_1*em.num[stride_count];i++)
					{
						printf("%c",*(char *)(compressed_data+i));
					}
					printf("\n-----------------------------------------------------------------------\n");
					//decompressed_data-=em.num[stride_count]*PAGE_SIZE_1;
					
					stride_len = (em.num[stride_count])*PAGE_SIZE_1;
					r = lzo1x_decompress(compressed_data,stride_len,data+(stride_count-1)*PAGE_SIZE_1*COMPRESSION_STRIDE_LEN,(lzo_uint *)&decompressed_length,NULL);
					if (r == LZO_E_OK)
						printf("decompressed %lu bytes back into %lu bytes\n",
									(unsigned long) decompressed_length, (unsigned long) stride_len);
					//len2=len2+decompressed_length;
					/*
					printf("----------------------------Decompressed data----------------------------\n");
					for(i=(stride_count-1)*PAGE_SIZE_1*COMPRESSION_STRIDE_LEN;i<stride_count*PAGE_SIZE_1*COMPRESSION_STRIDE_LEN;i++)
					{
						printf("%c",*(char *)(data+i));
					}
					printf("-----------------------------------------------------------------------\n");
					*/
					blocks_count=0;
					free(compressed_data);
					stride_count++;
				}
			}
			else
			{
				memcpy(data, bufdata(clone) + from, some);
			}
			
		}			
		trace_off("transfer %u bytes, block 0x%Lx, buffer %p",
			  some, bufindex(clone), buffer);
		blockput(clone);			
		if(stride_count==em.count && !write && is_compressed_file(inode))
		{
			tail=0;
			pos=0;
			break;
		}
		tail -= some;
		pos += some;
		if(write || !is_compressed_file(inode))
			data += some;
		printf("pos %u,some %u, tail %u",(unsigned int)pos,(unsigned int)some,(unsigned int)tail);
					
	}
	file->f_pos = pos;
	
	if (write) {
		if (inode->i_size < pos)
			inode->i_size = pos;
		tux3_mark_inode_dirty(inode);
	}					
	if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return err ? err : (!is_compressed_file(inode)?len - tail:len2);
}

int tuxread(struct file *file, void *data, unsigned len)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return tuxio(file, data, len, 0);
}

int tuxwrite(struct file *file, const void *data, unsigned len)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct sb *sb = file->f_inode->i_sb;
	int ret;
	change_begin(sb);
	ret = tuxio(file, (void *)data, len, 1);
	change_end(sb);
	if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return ret;
}

void tuxseek(struct file *file, loff_t pos)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	file->f_pos = pos;
	if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);}
}


int page_symlink(struct inode *inode, const char *symname, int len)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct file file = { .f_inode = inode, };
	int ret;

	assert(inode->i_size == 0);
	ret = tuxio(&file, (void *)symname, len, 1);
	if (ret < 0)
	{
		if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return ret;
	}
	if (len != ret)
	{
		if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return -EIO;
	}
	if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return 0;
}

int page_readlink(struct inode *inode, void *buf, unsigned size)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct file file = { .f_inode = inode, };
	unsigned len = min_t(loff_t, inode->i_size, size);
	int ret;

	ret = tuxread(&file, buf, len);
	if (ret < 0)
	{
		if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return ret;
	}
	if (ret != len)
	{
		if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return -EIO;
	}
	if(DEBUG_MODE_U==1){printf("\t\t\t\t%25s[U]  %25s  %4d  #out\n",__FILE__,__func__,__LINE__);};return 0;
}
