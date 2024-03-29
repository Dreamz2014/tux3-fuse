#include "tux3user.h"

#include "buffer.c"
#include "diskio.c"
#include "hexdump.c"

#ifndef trace
#define trace trace_on
#endif

#include "kernel/utility.c"

int devio(int rw, struct dev *dev, loff_t offset, void *data, unsigned len)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return ioabs(dev->fd, data, len, rw, offset);
}

int devio_vec(int rw, struct dev *dev, loff_t offset, struct iovec *iov,
	      unsigned iovcnt)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return iovabs(dev->fd, iov, iovcnt, rw, offset);
}

int blockio(int rw, struct sb *sb, struct buffer_head *buffer, block_t block)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	trace("%s: buffer %p, block %Lx",
	      (rw & WRITE) ? "write" : "read", buffer, block);
	return devio(rw, sb_dev(sb), block << sb->blockbits, bufdata(buffer),
		     sb->blocksize);
}

int blockio_vec(int rw, struct bufvec *bufvec, block_t block, unsigned count)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	trace("%s: bufvec %p, count %u, block %Lx",
	      (rw & WRITE) ? "write" : "read", bufvec, count, block);
	printf("inode : %Lu\n",tux_inode(bufvec->map->inode)->inum);//
	return bufvec_io(rw, bufvec, block, count);
}

/*
 * Message helpers
 */

void __tux3_msg(struct sb *sb, const char *level, const char *prefix,
		const char *fmt, ...)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

void __tux3_fs_error(struct sb *sb, const char *func, unsigned int line,
		     const char *fmt, ...)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	va_list args;

	printf("Error: %s:%d: ", func, line);
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	printf("\n");

	assert(0);		/* FIXME: what to do here? */
}

void __tux3_dbg(const char *fmt, ...)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}
