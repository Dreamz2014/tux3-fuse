

#if TUX3_FLUSHER == TUX3_FLUSHER_ASYNC_HACK
#include "tux3.h"
#include <linux/kthread.h>
#include <linux/freezer.h>

/*
 * FIXME: this is hack to override writeback without patch kernel.
 * We should add proper interfaces to do this, instead. Then, remove
 * this stuff.
 */

void tux3_set_mapping_bdi(struct inode *inode)
{
	/*
	 * Hack: set backing_dev_info to use our bdi.
	 */
	inode->i_mapping->backing_dev_info = inode->i_sb->s_bdi;
}

/*
 * FIXME: dirty hack for now. We should add callback in writeback task
 * instead of custom bdi.
 */
struct wb_writeback_work {
	long nr_pages;
	struct super_block *sb;
	unsigned long *older_than_this;
	enum writeback_sync_modes sync_mode;
	unsigned int tagged_writepages:1;
	unsigned int for_kupdate:1;
	unsigned int range_cyclic:1;
	unsigned int for_background:1;
	enum wb_reason reason;		/* why was writeback initiated? */

	struct list_head list;		/* pending work list */
	struct completion *done;	/* set if the caller waits */
};

/* Do the delta transition until specified delta */
static int try_delta_transition_until_delta(struct sb *sb, unsigned delta)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	trace("delta %u, marshal %u, backend_state %lx",
	      delta, sb->marshal_delta, sb->backend_state);

	/* Already delta transition was started for delta */
	if (delta_after_eq(sb->marshal_delta, delta))
		return 1;

	if (!test_and_set_bit(TUX3_COMMIT_RUNNING_BIT, &sb->backend_state)) {
		/* Recheck after grabed TUX3_COMMIT_RUNNING_BIT */
		if (delta_after_eq(sb->marshal_delta, delta)) {
			clear_bit(TUX3_COMMIT_RUNNING_BIT, &sb->backend_state);
			return 1;
		}

		delta_transition(sb);
	}

	return delta_after_eq(sb->marshal_delta, delta);
}

/* Advance delta transition until specified delta */
static int wait_for_transition(struct sb *sb, unsigned delta)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return wait_event_killable(sb->delta_event_wq,
				   try_delta_transition_until_delta(sb, delta));
}

static long tux3_wb_writeback(struct bdi_writeback *wb,
			      struct wb_writeback_work *work)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct sb *sb = container_of(wb->bdi, struct sb, bdi);
	struct delta_ref *delta_ref;
	unsigned delta;
	int err;

	/* If we didn't finish replay yet, don't flush. */
	if (!(vfs_sb(sb)->s_flags & MS_ACTIVE))
		return 0;

	/* Get delta that have to write */
	delta_ref = delta_get(sb);
#ifdef UNIFY_DEBUG
	/* NO_UNIFY and FORCE_UNIFY are not supported for now */
	delta_ref->unify_flag = ALLOW_UNIFY;
#endif
	delta = delta_ref->delta;
	delta_put(sb, delta_ref);

	/* Make sure the delta transition was done for current delta */
	err = wait_for_transition(sb, delta);
	if (err)
		return err;
	assert(delta_after_eq(sb->marshal_delta, delta));

	/* Wait for last referencer of delta was gone */
	wait_event(sb->delta_event_wq,
		   test_bit(TUX3_COMMIT_PENDING_BIT, &sb->backend_state));

	if (test_bit(TUX3_COMMIT_PENDING_BIT, &sb->backend_state)) {
		clear_bit(TUX3_COMMIT_PENDING_BIT, &sb->backend_state);

		err = flush_delta(sb);
		/* FIXME: error handling */
#if 0
		/* wb_update_bandwidth() is not exported to module */
		wb_update_bandwidth(wb, wb_start);
#endif
	}

	return 1; /* FIXME: return code */
}

static bool inode_dirtied_after(struct inode *inode, unsigned long t)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	bool ret = time_after(inode->dirtied_when, t);
#ifndef CONFIG_64BIT
	/*
	 * For inodes being constantly redirtied, dirtied_when can get stuck.
	 * It _appears_ to be in the future, but is actually in distant past.
	 * This test is necessary to prevent such wrapped-around relative times
	 * from permanently stopping the whole bdi writeback.
	 */
	ret = ret && time_before_eq(inode->dirtied_when, jiffies);
#endif
	return ret;
}

static int tux3_has_old_data(struct bdi_writeback *wb)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	static unsigned int tux3_dirty_expire_interval = 30 * 100;

	int has_old = 0;

	/*
	 * We don't flush for each inodes. So, we flush all for each
	 * tux3_dirty_expire_interval.
	 *
	 * FIXME: we should pickup only older inodes?
	 */
	spin_lock(&wb->list_lock);
	if (wb_has_dirty_io(wb)) {
		unsigned long older_than_this = jiffies -
			msecs_to_jiffies(tux3_dirty_expire_interval * 10);
		struct inode *inode =
			list_entry(wb->b_dirty.prev, struct inode, i_wb_list);

		if (!inode_dirtied_after(inode, older_than_this))
			has_old = 1;
	}
	spin_unlock(&wb->list_lock);

	return has_old;
}

static long tux3_wb_check_old_data_flush(struct bdi_writeback *wb)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* Hack: dirty_expire_interval is not exported to module */
	unsigned long expired;

	/*
	 * When set to zero, disable periodic writeback
	 */
	if (!dirty_writeback_interval)
		return 0;

	expired = wb->last_old_flush +
			msecs_to_jiffies(dirty_writeback_interval * 10);
	if (time_before(jiffies, expired))
		return 0;

	wb->last_old_flush = jiffies;

	if (!tux3_has_old_data(wb)) {
		/*
		 * If now after interval, we return 1 at least, to
		 * avoid to run tux3_wb_check_background_flush().
		 */
		return 1;
	}

	struct wb_writeback_work work = {
		.nr_pages	= 0,
		.sync_mode	= WB_SYNC_NONE,
		.for_kupdate	= 1,
		.range_cyclic	= 1,
		.reason		= WB_REASON_PERIODIC,
	};

	return tux3_wb_writeback(wb, &work);
}

static inline int tux3_over_bground_thresh(struct backing_dev_info *bdi,
					   long wrote)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/*
	 * FIXME: Memory pressure functions are not exported to module.
	 *
	 * So, if we didn't wrote any data on this wakeup, we assume
	 * this wakeup call is from memory pressure.
	 */
	return !wrote;
}

static long tux3_wb_check_background_flush(struct bdi_writeback *wb, long wrote)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	if (tux3_over_bground_thresh(wb->bdi, wrote)) {

		struct wb_writeback_work work = {
			.nr_pages	= LONG_MAX,
			.sync_mode	= WB_SYNC_NONE,
			.for_background	= 1,
			.range_cyclic	= 1,
			.reason		= WB_REASON_BACKGROUND,
		};

		return tux3_wb_writeback(wb, &work);
	}

	return 0;
}

static struct wb_writeback_work *
get_next_work_item(struct backing_dev_info *bdi)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct wb_writeback_work *work = NULL;

	spin_lock_bh(&bdi->wb_lock);
	if (!list_empty(&bdi->work_list)) {
		work = list_entry(bdi->work_list.next,
				  struct wb_writeback_work, list);
		list_del_init(&work->list);
	}
	spin_unlock_bh(&bdi->wb_lock);
	return work;
}

static long tux3_do_writeback(struct bdi_writeback *wb, int force_wait)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct backing_dev_info *bdi = wb->bdi;
	struct wb_writeback_work *work = NULL;
	long wrote = 0;

	set_bit(BDI_writeback_running, &wb->bdi->state);
	while ((work = get_next_work_item(bdi)) != NULL) {
		trace("nr_pages %ld, sb %p, sync_mode %d, "
		      "tagged_writepages %d, for_kupdate %d, range_cyclic %d, "
		      "for_background %d, reason %d, done %p",
		      work->nr_pages, work->sb, work->sync_mode,
		      work->tagged_writepages, work->for_kupdate,
		      work->range_cyclic, work->for_background,
		      work->reason, work->done);

		/*
		 * Override sync mode, in case we must wait for completion
		 * because this thread is exiting now.
		 */
		if (force_wait)
			work->sync_mode = WB_SYNC_ALL;

		wrote += tux3_wb_writeback(wb, work);

		/*
		 * Notify the caller of completion if this is a synchronous
		 * work item, otherwise just free it.
		 */
		if (work->done)
			complete(work->done);
		else
			kfree(work);
	}
	trace("flush done");

	/*
	 * Check for periodic writeback, kupdated() style
	 */
	wrote += tux3_wb_check_old_data_flush(wb);
	wrote += tux3_wb_check_background_flush(wb, wrote);
	clear_bit(BDI_writeback_running, &wb->bdi->state);

	return wrote;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#define TUX3_BDI_CAP	(BDI_CAP_NO_WRITEBACK | BDI_CAP_MAP_COPY)

/*
 * HACK: We set BDI_CAP_NO_WRITEBACK to bdi to disable flusher task
 * management to overwrite flusher task.
 *
 * But, BDI_CAP_NO_WRITEBACK disables also some functionality. So, we
 * replace some check by own check.
 *
 * FIXME: Remove this file after implement of flusher interface
 */

void tux3_accout_set_writeback(struct page *page)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/*
	 * Hack: BDI_CAP_NO_WRITEBACK disables this accouting, so we
	 * do ourself instead.
	 */
	struct address_space *mapping = page->mapping;
	if (mapping)
		inc_bdi_stat(mapping->backing_dev_info, BDI_WRITEBACK);
}

void tux3_accout_clear_writeback(struct page *page)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/*
	 * Hack: BDI_CAP_NO_WRITEBACK disables this accouting, so we
	 * do ourself instead.
	 */
	struct address_space *mapping = page->mapping;
	if (mapping) {
		dec_bdi_stat(mapping->backing_dev_info, BDI_WRITEBACK);
		bdi_writeout_inc(mapping->backing_dev_info);
	}
}

/*
 * If dirty inode list was empty, we start the timer for periodical
 * flush.  (based on bdi_wakeup_thread_delayed())
 *
 * Hack: BDI_CAP_NO_WRITEBACK disables to wakeup flusher. So, we do
 * ourself instead.
 */
void tux3_start_periodical_flusher(struct sb *sb)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct backing_dev_info *bdi = vfs_sb(sb)->s_bdi;
	unsigned long timeout;

	timeout = msecs_to_jiffies(dirty_writeback_interval * 10);
	mod_timer(&bdi->wb.wakeup_timer, jiffies + timeout);
}

/*
 * Handle writeback of dirty data for the device backed by this bdi. Also
 * wakes up periodically and does kupdated style flushing.
 */
static int tux3_writeback_thread(void *data)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct bdi_writeback *wb = data;
	struct backing_dev_info *bdi = wb->bdi;
	long pages_written;

	current->flags |= PF_SWAPWRITE;
	set_freezable();
	wb->last_active = jiffies;

	/*
	 * Our parent may run at a different priority, just set us to normal
	 */
	set_user_nice(current, 0);

	while (!kthread_freezable_should_stop(NULL)) {
		/*
		 * Remove own delayed wake-up timer, since we are already awake
		 * and we'll take care of the preriodic write-back.
		 */
		del_timer(&wb->wakeup_timer);
#if 0
		{
			struct inode *inode;
			spin_lock(&wb->list_lock);
			list_for_each_entry(inode, &wb->b_dirty, i_wb_list) {
				trace("inum %Lx", tux_inode(inode)->inum);
			}
			spin_unlock(&wb->list_lock);
		}
#endif
		pages_written = tux3_do_writeback(wb, 0);

		if (pages_written)
			wb->last_active = jiffies;

		set_current_state(TASK_INTERRUPTIBLE);
		if (!list_empty(&bdi->work_list) || kthread_should_stop()) {
			__set_current_state(TASK_RUNNING);
			continue;
		}

		if (wb_has_dirty_io(wb) && dirty_writeback_interval)
			schedule_timeout(msecs_to_jiffies(dirty_writeback_interval * 10));
		else {
			/*
			 * We have nothing to do, so can go sleep without any
			 * timeout and save power. When a work is queued or
			 * something is made dirty - we will be woken up.
			 */
			schedule();
		}
	}

	/* Flush any work that raced with us exiting */
	if (!list_empty(&bdi->work_list))
		tux3_do_writeback(wb, 1);

	return 0;
}

static int tux3_setup_writeback(struct sb *sb, struct backing_dev_info *bdi)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	dev_t dev = vfs_sb(sb)->s_bdev->bd_dev;
	struct task_struct *task;

	task = kthread_create(tux3_writeback_thread, &bdi->wb,
			      "flush-%s-%u:%u",
			      bdi->name, MAJOR(dev), MINOR(dev));
	if (IS_ERR(task))
		return PTR_ERR(task);

	/*
	 * The spinlock makes sure we do not lose wake-ups when racing
	 * with 'bdi_queue_work()'.  And as soon as the bdi thread is
	 * visible, we can start it.
	 */
	spin_lock_bh(&bdi->wb_lock);
	bdi->wb.task = task;
	spin_unlock_bh(&bdi->wb_lock);

	return 0;
}

static void tux3_wakeup_writeback(struct backing_dev_info *bdi)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	wake_up_process(bdi->wb.task);
}

static void tux3_destroy_writeback(struct backing_dev_info *bdi)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/*
	 * If bdi has BDI_CAP_NO_WRITEBACK, bdi_destory() doesn't
	 * cleanup bdi. So, this remove it from capabilities.
	 */
	bdi->capabilities &= ~BDI_CAP_NO_WRITEBACK;
	bdi_destroy(bdi);
}
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0) */
#define TUX3_BDI_CAP	BDI_CAP_MAP_COPY

void tux3_accout_set_writeback(struct page *page)
{
}

void tux3_accout_clear_writeback(struct page *page)
{
}

void tux3_start_periodical_flusher(struct sb *sb)
{
}

/* Dirty hack to get bdi_wq address from module */
static struct workqueue_struct *kernel_bdi_wq;

/*
 * Handle writeback of dirty data for the device backed by this bdi. Also
 * reschedules periodically and does kupdated style flushing.
 */
static void tux3_writeback_workfn(struct work_struct *work)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct bdi_writeback *wb = container_of(to_delayed_work(work),
						struct bdi_writeback, dwork);
	struct backing_dev_info *bdi = wb->bdi;
	long pages_written;

#if 0
	/* set_worker_desc() is not exported to module */
	set_worker_desc("flush-tux3-%s", dev_name(bdi->dev));
#endif
	current->flags |= PF_SWAPWRITE;

#if 0
	/* current_is_workqueue_rescuer() is not exported to module */
	if (likely(!current_is_workqueue_rescuer() ||
		   list_empty(&bdi->bdi_list)))
#endif
	{
		/*
		 * The normal path.  Keep writing back @bdi until its
		 * work_list is empty.  Note that this path is also taken
		 * if @bdi is shutting down even when we're running off the
		 * rescuer as work_list needs to be drained.
		 */
		do {
			pages_written = tux3_do_writeback(wb, 0);
//			trace_writeback_pages_written(pages_written);
		} while (!list_empty(&bdi->work_list));
	}
#if 0
	else {
		/*
		 * bdi_wq can't get enough workers and we're running off
		 * the emergency worker.  Don't hog it.  Hopefully, 1024 is
		 * enough for efficient IO.
		 */
		pages_written = writeback_inodes_wb(&bdi->wb, 1024,
						    WB_REASON_FORKER_THREAD);
		trace_writeback_pages_written(pages_written);
	}
#endif
	if (!list_empty(&bdi->work_list) ||
	    (wb_has_dirty_io(wb) && dirty_writeback_interval))
		queue_delayed_work(kernel_bdi_wq, &wb->dwork,
			msecs_to_jiffies(dirty_writeback_interval * 10));

	current->flags &= ~PF_SWAPWRITE;
}

#include <linux/kallsyms.h>
static int tux3_setup_writeback(struct sb *sb, struct backing_dev_info *bdi)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* Dirty hack to get bdi_wq address from module */
	if (kernel_bdi_wq == NULL) {
		unsigned long wq_addr;

		wq_addr = kallsyms_lookup_name("bdi_wq");
		if (!wq_addr) {
			tux3_err(sb, "couldn't find bdi_wq address\n");
			return -EINVAL;
		}
		kernel_bdi_wq = *(struct workqueue_struct **)wq_addr;
		tux3_msg(sb, "use bdi_wq %p", kernel_bdi_wq);
	}

	/* Overwrite callback by ourself handler */
	INIT_DELAYED_WORK(&bdi->wb.dwork, tux3_writeback_workfn);

	return 0;
}

static void tux3_wakeup_writeback(struct backing_dev_info *bdi)
{
}

static void tux3_destroy_writeback(struct backing_dev_info *bdi)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	bdi_destroy(bdi);
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0) */

static int tux3_congested_fn(void *congested_data, int bdi_bits)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	return bdi_congested(congested_data, bdi_bits);
}

/*
 * We need to disable writeback to control dirty flags of inode.
 * Otherwise, writeback will clear dirty, and inode can be reclaimed
 * without our control.
 */
int tux3_init_flusher(struct sb *sb)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct backing_dev_info *bdi = &sb->bdi;
	int err;

	bdi->ra_pages		= vfs_sb(sb)->s_bdi->ra_pages;
	bdi->congested_fn	= tux3_congested_fn;
	bdi->congested_data	= vfs_sb(sb)->s_bdi;

	err = bdi_setup_and_register(bdi, "tux3", TUX3_BDI_CAP);
	if (err)
		return err;

	err = tux3_setup_writeback(sb, bdi);
	if (err) {
		bdi_destroy(bdi);
		return err;
	}

	vfs_sb(sb)->s_bdi = bdi;

	tux3_wakeup_writeback(bdi);

	return 0;
}

void tux3_exit_flusher(struct sb *sb)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	struct backing_dev_info *bdi = vfs_sb(sb)->s_bdi;
	tux3_destroy_writeback(bdi);
}

static void schedule_flush_delta(struct sb *sb)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* Wake up waiters for pending marshal delta */
	wake_up_all(&sb->delta_event_wq);
}

static void try_delta_transition(struct sb *sb)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
#if 0
	trace("marshal %u, backend_state %lx",
	      sb->marshal_delta, sb->backend_state);
	sync_inodes_sb(vfs_sb(sb));
#endif
}

static int sync_current_delta(struct sb *sb, enum unify_flags unify_flag)
{
	if(DEBUG_MODE_K==1)
	{
		printf("\t\t\t\t%25s[K]  %25s  %4d  #in\n",__FILE__,__func__,__LINE__);
	}
	/* FORCE_UNIFY is not supported */
	WARN_ON(unify_flag == FORCE_UNIFY);
	/* This is called only for fsync, so we can take ->s_umount here */
	down_read(&vfs_sb(sb)->s_umount);
	sync_inodes_sb(vfs_sb(sb));
	up_read(&vfs_sb(sb)->s_umount);
	return 0;	/* FIXME: error code */
}
#endif /* TUX3_FLUSHER != TUX3_FLUSHER_ASYNC_HACK */
