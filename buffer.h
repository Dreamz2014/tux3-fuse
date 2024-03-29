#ifndef BUFFER_H
#define BUFFER_H

#define BUFFER_FOR_TUX3

#ifdef BUFFER_FOR_TUX3
#include "tux3user.h"
#include "trace.h"

static inline unsigned tux3_delta(unsigned delta);
#endif
#include "kernel/tux3_fork.h"
#include "libklib/list.h"
#include <sys/uio.h>

#ifdef BUFFER_FOR_TUX3
/* Maximum delta number (must be power of 2) */
#define BUFFER_DIRTY_STATES	2	/* 1 frontend + 1 backend */
#define BUFFER_INIT_DELTA	0	/* initial delta number */
#define TUX3_MAX_DELTA		BUFFER_DIRTY_STATES
#define TUX3_INIT_DELTA		BUFFER_INIT_DELTA
#else
/* Maximum delta number (must be power of 2) */
#define BUFFER_DIRTY_STATES	4	/* 1 frontend + 1 backend */
#define BUFFER_INIT_DELTA	0	/* initial delta number */
#endif

enum {
	BUFFER_FREED, BUFFER_EMPTY, BUFFER_CLEAN, BUFFER_DIRTY,
	BUFFER_STATES = BUFFER_DIRTY + BUFFER_DIRTY_STATES
};

/* Define buffer fork state helpers */
#define BUFFER_STATE_BITS	order_base_2(BUFFER_STATES)
TUX3_DEFINE_STATE_FNS(unsigned, buf, BUFFER_DIRTY, BUFFER_STATE_BITS, 0);

#define BUFFER_BUCKETS 999

// disk io address range
#ifdef BUFFER_FOR_TUX3
/*
 * Choose carefully:
 * loff_t can be "long" or "long long" in userland. (not printf friendly)
 * sector_t can be "unsigned long" or "u64". (32bits arch 32bits is too small)
 *
 * we want 48bits for tux3, and error friendly. (FIXME: u64 is better?)
 */
typedef signed long long	block_t;
#else
typedef loff_t			block_t;
#endif

struct dev { unsigned fd, bits; };

struct buffer_head;
struct bufvec;

typedef int (blockio_t)(int rw, struct bufvec *bufvec);

struct map {
#ifdef BUFFER_FOR_TUX3
	struct inode *inode;
#endif
	struct dev *dev;
	blockio_t *io;
	struct hlist_head hash[BUFFER_BUCKETS];
};

typedef struct map map_t;

struct buffer_head {
	map_t *map;
	struct hlist_node hashlink;
	struct list_head link;
	struct list_head lru; /* used for LRU list and the free list */
	unsigned count, state;
	block_t index;
	void *data;
};

static inline void *bufdata(struct buffer_head *buffer)
{
	return buffer->data;
}

static inline unsigned bufsize(struct buffer_head *buffer)
{
	return 1 << buffer->map->dev->bits;
}

static inline block_t bufindex(struct buffer_head *buffer)
{
	return buffer->index;
}

static inline int bufcount(struct buffer_head *buffer)
{
	return buffer->count;
}

static inline int buffer_empty(struct buffer_head *buffer)
{
	return buffer->state == BUFFER_EMPTY;
}

static inline int buffer_clean(struct buffer_head *buffer)
{
	return buffer->state == BUFFER_CLEAN;
}

static inline int buffer_dirty(struct buffer_head *buffer)
{
	return tux3_bufsta_has_delta(buffer->state);
}

/* Check whether buffer was already dirtied atomically for delta */
static inline int buffer_already_dirty(struct buffer_head *buffer,
				       unsigned delta)
{
	unsigned state = buffer->state;
	/* If buffer had same delta, buffer was already dirtied for delta */
	return tux3_bufsta_has_delta(state) &&
		tux3_bufsta_get_delta(state) == tux3_delta(delta);
}

/* Check whether we can modify buffer atomically for delta */
static inline int buffer_can_modify(struct buffer_head *buffer, unsigned delta)
{
	unsigned state = buffer->state;
	/* If buffer is clean or dirtied for same delta, we can modify */
	return !tux3_bufsta_has_delta(state) ||
		tux3_bufsta_get_delta(state) == tux3_delta(delta);
}

struct sb;
struct tux3_iattr_data;
struct buffer_head *new_buffer(map_t *map);
void show_buffer(struct buffer_head *buffer);
void show_buffers(map_t *map);
void show_active_buffers(map_t *map);
void show_dirty_buffers(map_t *map);
void show_buffers_state(unsigned state);
void set_buffer_state_list(struct buffer_head *buffer, unsigned state, struct list_head *list);
void tux3_set_buffer_dirty_list(map_t *map, struct buffer_head *buffer,
				int delta, struct list_head *head);
void tux3_set_buffer_dirty(map_t *map, struct buffer_head *buffer, int delta);
struct buffer_head *set_buffer_dirty(struct buffer_head *buffer);
struct buffer_head *set_buffer_clean(struct buffer_head *buffer);
struct buffer_head *__set_buffer_empty(struct buffer_head *buffer);
struct buffer_head *set_buffer_empty(struct buffer_head *buffer);
void tux3_clear_buffer_dirty(struct buffer_head *buffer, unsigned delta);
void clear_buffer_dirty_for_endio(struct buffer_head *buffer, int err);
void get_bh(struct buffer_head *buffer);
void blockput_free(struct sb *sb, struct buffer_head *buffer);
void blockput_free_unify(struct sb *sb, struct buffer_head *buffer);
void blockput(struct buffer_head *buffer);
unsigned buffer_hash(block_t block);
struct buffer_head *peekblk(map_t *map, block_t block);
struct buffer_head *blockget(map_t *map, block_t block);
struct buffer_head *blockread(map_t *map, block_t block);
void insert_buffer_hash(struct buffer_head *buffer);
void remove_buffer_hash(struct buffer_head *buffer);
void truncate_buffers_range(map_t *map, loff_t lstart, loff_t lend);
void invalidate_buffers(map_t *map);
void init_buffers(struct dev *dev, unsigned poolsize, int debug);
int __tux3_volmap_io(int rw, struct bufvec *bufvec, block_t block,
		     unsigned count);
int dev_errio(int rw, struct bufvec *bufvec);
map_t *new_map(struct dev *dev, blockio_t *io);
void free_map(map_t *map);

/* buffer_writeback.c */
/* Helper for waiting I/O (stub) */
struct iowait {
};

/* I/O completion callback */
typedef void (*bufvec_end_io_t)(struct buffer_head *buffer, int err);

/* Helper for buffer vector I/O */
struct bufvec {
	struct list_head *buffers;	/* The dirty buffers for this delta */
	struct list_head contig;	/* One logical contiguous range */
	unsigned contig_count;		/* Count of contiguous buffers */
	struct list_head compress;
	unsigned compress_count;
	block_t global_index;
	struct tux3_iattr_data *idata;	/* inode attrs for write */
	map_t *map;			/* map for dirty buffers */

	struct list_head for_io;	/* The buffers in iovec */

	bufvec_end_io_t end_io;
};

static inline struct inode *bufvec_inode(struct bufvec *bufvec)
{
	return bufvec->map->inode;
}

static inline unsigned bufvec_contig_count(struct bufvec *bufvec)
{
	return bufvec->contig_count;
}

static inline struct buffer_head *bufvec_contig_buf(struct bufvec *bufvec)
{
	struct list_head *first = bufvec->contig.next;
	assert(!list_empty(&bufvec->contig));
	return list_entry(first, struct buffer_head, link);
}

/* buffer for each contiguous buffers */
#define bufvec_buffer_for_each_contig(b, v)	\
	list_for_each_entry(b, &(v)->contig, link)

static inline block_t bufvec_contig_index(struct bufvec *bufvec)
{
	return bufindex(bufvec_contig_buf(bufvec));
}

static inline block_t bufvec_contig_last_index(struct bufvec *bufvec)
{
	return bufvec_contig_index(bufvec) + bufvec_contig_count(bufvec) - 1;
}

void tux3_iowait_init(struct iowait *iowait);
void tux3_iowait_wait(struct iowait *iowait);
void bufvec_init(struct bufvec *bufvec, map_t *map,
		 struct list_head *head, struct tux3_iattr_data *idata);
void bufvec_free(struct bufvec *bufvec);
int bufvec_io(int rw, struct bufvec *bufvec, block_t physical, unsigned count);
void bufvec_complete_without_io(struct bufvec *bufvec, unsigned count);
int bufvec_contig_add(struct bufvec *bufvec, struct buffer_head *buffer);
int flush_list(map_t *map, struct tux3_iattr_data *idata,
	       struct list_head *head);

/* block_fork.c */
static inline int buffer_forked(struct buffer_head *buffer)
{
	/* no async backend, so frontend never grab forked buffer */
	assert(!hlist_unhashed(&buffer->hashlink));
	return 0;
}

void free_forked_buffers(struct sb *sb, struct inode *inode, int force);
struct buffer_head *blockdirty(struct buffer_head *buffer, unsigned newdelta);
int bufferfork_to_invalidate(map_t *map, struct buffer_head *buffer);
#endif
