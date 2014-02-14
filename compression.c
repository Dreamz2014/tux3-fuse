#ifndef COMPRESSION_C
#define COMPRESSION_C

//#include "RLE.h"
#include <lzo/lzo1x.h>

struct workspace
{
	void *mem;		//memory required for compression
	void *c_buf;	//memory where compressed buffer goes
	void *d_buf;	//memory where decompressed buffer goes
};

static struct workspace *init_workspace(unsigned stride_len)
{
	if(DEBUG_MODE_K==1)
	{
		printf("%25s  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct workspace *workspace;

	workspace = malloc(sizeof(*workspace));
	if(!workspace)
		return ERR_PTR(-ENOMEM);

	workspace->mem = malloc(LZO1X_MEM_COMPRESS);
	workspace->c_buf = malloc(PAGE_SIZE_1*stride_len);
	workspace->d_buf = malloc(PAGE_SIZE_1*stride_len);

	if (!workspace->mem || !workspace->d_buf || !workspace->c_buf)
		goto fail;

	return workspace;
	
	fail:
	return ERR_PTR(-ENOMEM);
}

static void free_workspace(struct workspace *workspace)
{
	if(DEBUG_MODE_K==1)
	{
		printf("%25s  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	
	free(workspace->mem);
	free(workspace->c_buf);
	free(workspace->d_buf);
	free(workspace);
}

int compress_stride(struct bufvec *bufvec)
{
	if(DEBUG_MODE_K==1)
	{
		printf("%25s  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct inode *inode = bufvec_inode(bufvec);
	struct buffer_head *buffer;
	struct workspace *workspace;
	struct list_head *list;
	unsigned offset, out_blocks;
	unsigned len = bufvec_contig_count(bufvec);
	unsigned in_len, tail, out_len;
	char *data;
	int ret = 0;
	
	workspace = init_workspace(len);
	printf("\n[C]inode : %Lu", tux_inode(inode)->inum);
	
	in_len = bufvec_contig_count(bufvec)*PAGE_SIZE_1;
	out_len = 0;

	offset = 0;
	while(len)
	{
		list = bufvec->contig.next;
		buffer = list_entry(list, struct buffer_head, link);

		data = (char *)buffer->data;		
		memcpy((char *)workspace->d_buf + offset, data, PAGE_SIZE_1);
		offset += PAGE_SIZE_1;
		
		list_move_tail(&buffer->link, &bufvec->compress);
		bufvec->contig_count--;
		bufvec->compress_count++;
		len--;
	}
	
	if (lzo_init() != LZO_E_OK)
    {
        printf("internal error - lzo_init() failed !!!\n");
        printf("(this usually indicates a compiler bug - try recompiling\nwithout optimizations, and enable '-DLZO_DEBUG' for diagnostics)\n");
        return 3;
    }

	ret =  lzo1x_1_compress(workspace->d_buf, in_len, workspace->c_buf, (lzo_uint *)&out_len, workspace->mem);
	if (ret == LZO_E_OK)
		printf("\nSTRIDE SUCCESSFULLY COMPRESSED !");
		
	out_blocks = out_len / PAGE_SIZE_1 + 1;
	tail = PAGE_SIZE_1 - (out_len % PAGE_SIZE_1);
	printf("\n\nCompressed from %u to %u | Compressed blocks : %u | tail : %u\n", in_len, (unsigned)out_len, out_blocks, tail);
	memset((char *)workspace->c_buf + out_len, 0, tail);
	
	offset = 0;
	while(out_blocks > 0)
	{
		list = bufvec->compress.next;
		buffer = list_entry(list, struct buffer_head, link);
		//printk(KERN_INFO "Move_to_contig : %Lu",bufindex(buffer));

		data = (char *)buffer->data;
		memcpy(data, (char *)workspace->c_buf + offset, PAGE_SIZE_1);
		buffer->index = bufvec->global_index++;
		offset += PAGE_SIZE_1;
		
		list_move_tail(&buffer->link, &bufvec->contig);
		bufvec->contig_count++;
		bufvec->compress_count--;
		
		out_blocks--;
	}

	free_workspace(workspace);
	return ret;
}

int test;
struct stride_map
{
	unsigned int num[100];
	unsigned int count;
}em;
//unsigned int modulus;
//unsigned int logical_memory;
unsigned int is_first;
void init_stride(void)
{
	em.count=0;
}

void add_stride(unsigned int i)
{
	printf("i %d\n",i);
	if(em.count==0)
	{
		em.num[em.count]=i;
	}
	else
	{
		em.num[em.count]=i-em.num[em.count-1];
		printf("em.num[em.count-1] %d\n",em.num[em.count-1]);
	}
	printf("ADDSTRIDE stride no : %d, stride len : %d\n",em.count,em.num[em.count]);
	em.count++;
}

int is_stride_initialised(void)
{
	return em.count;
}

#endif
