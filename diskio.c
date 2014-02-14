#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/fs.h> // for BLKGETSIZE
#include <sys/ioctl.h>
#include <sys/stat.h>
#include "trace.h"
#include "diskio.h"

static ssize_t iov_length(struct iovec *iov, int iovcnt)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	ssize_t size = 0;
	int i;

	for (i = 0; i < iovcnt; i++)
		size += iov[i].iov_len;
	return size;
}

int iovabs(int fd, struct iovec *iov, int iovcnt, int out, off_t offset)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	while (1) {
		int count = min(iovcnt, UIO_MAXIOV);
		ssize_t ret;

		if (out)
		{
			//newFunction Begin
			if(DEBUG_RW==1)
			{
				printf("\n\n------------------------------IOVABS-PWRITEV------------------------------\n");
				char *c=iov->iov_base;
				int i;
				FILE *f;
				f=fopen("iovabs","a");
				fprintf(f,"\nWrite:\n");
				for(i=0;i<iov->iov_len;i++)
				{
					if((i)%32==0)
					{
						fprintf(f,"\n");
					}
					printf("%c",*c);
					fprintf(f,"%3u",*c);
					c++;
				}
				
				printf("\n\n--------------------------------------------------------------------------\n");
			}
			//newFunction End
			ret = pwritev(fd, iov, count, offset);
		}
		else
		{
			ret = preadv(fd, iov, count, offset);
			//newFunction Begin
			if(DEBUG_RW==1)
			{
				printf("\n\n------------------------------IOVABS-PREADV------------------------------\n");
				char *c=iov->iov_base;
				int i;
				FILE *f;
				f=fopen("iovabs","a");
				fprintf(f,"\nRead:\n");
				for(i=0;i<iov->iov_len;i++)
				{
					if((i)%32==0)
					{
						fprintf(f,"\n");
					}
					printf("%c",*c);
					fprintf(f,"%3u",*c);
					c++;
				}
				printf("\n\n-------------------------------------------------------------------------\n");
			}
			//newFunction End
		}
		if (ret == -1) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			return -errno;
		}
		if (ret != iov_length(iov, count))
			return -EIO;
		if (iovcnt == count)
			break;

		iov += count;
		iovcnt -= count;
		offset += ret;
	}

	return 0;
}

int ioabs(int fd, void *data, size_t count, int out, off_t offset)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	while (count) {
		ssize_t ret;
		if (out)
		{
			//newFunction Begin
			if(DEBUG_RW==1)
			{
				printf("\n\n------------------------------IOABS-PWRITEV------------------------------\n");
				char *c=data;
				int i;
				FILE *f;
				f=fopen("ioabs","a");
				fprintf(f,"\npwrite:\n");
				for(i=0;i<count;i++)
				{
					if((i)%32==0)
					{
						fprintf(f,"\n");
					}
					printf("%c",*c);
					fprintf(f,"%3u",*c);
					c++;
				}
				printf("\n\n-------------------------------------------------------------------------\n");
			}
			//newFunction End
			ret = pwrite(fd, data, count, offset);
		}
		else
		{
			ret = pread(fd, data, count, offset);
			//newFunction Begin
			if(DEBUG_RW==1)
			{
				printf("\n\n------------------------------IOABS-PREADV------------------------------\n");
				char *c=data;
				int i;
				FILE *f;
				f=fopen("ioabs","a");
				fprintf(f,"\nPread:\n");
				for(i=0;i<count;i++)
				{
					if((i)%32==0)
					{
						fprintf(f,"\n");
					}
					printf("%c",*c);
					fprintf(f,"%3u",*c);
					c++;
				}
				printf("\n\n------------------------------------------------------------------------\n");
			}
			//newFunction End
		}
		if (ret == -1) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			return -errno;
		}
		if (ret == 0)
			return -EIO;
		data += ret;
		count -= ret;
		offset += ret;
	}
	return 0;
}

static int iorel(int fd, void *data, size_t count, int out)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	while (count) {
		ssize_t ret;
		if (out)
			ret = write(fd, data, count);
		else
			ret = read(fd, data, count);
		if (ret == -1) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			return -errno;
		}
		if (ret == 0)
			return -EIO;
		data += ret;
		count -= ret;
	}
	return 0;
}

int diskread(int fd, void *data, size_t count, off_t offset)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return ioabs(fd, data, count, 0, offset);
}

int diskwrite(int fd, void *data, size_t count, off_t offset)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return ioabs(fd, data, count, 1, offset);
}

int streamread(int fd, void *data, size_t count)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return iorel(fd, data, count, 0);
}

int streamwrite(int fd, void *data, size_t count)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return iorel(fd, data, count, 1);
}

int fdsize64(int fd, loff_t *size)
{
	if(DEBUG_MODE_U==1)
	{
		printf("\t\t\t\t%25s[U]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct stat stat;
	if (fstat(fd, &stat))
		return -1;
	if (S_ISREG(stat.st_mode)) {
		*size = stat.st_size;
		return 0;
	}
	return ioctl(fd, BLKGETSIZE64, size);
}
