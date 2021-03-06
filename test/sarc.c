
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/delay.h>



int  sequential_random_io(struct cache_c *dmc, struct bio *bio)
{
    struct sequential_io *seqio;
    int sequential = 0; /* Saw > 1 in a row? */
    int skip       = 0; /* Enough sequential to hit the threshold */

    /* sysctl skip sequential threshold = 0 : disable, cache all sequential and random i/o.
     * This is the default. */   
    if (dmc->sysctl_skip_seq_thresh_kb == 0)
        return 0;

    /* Is it a continuation of recent i/o?  Try to find a match.  */
    DPRINTK("skip_sequential_io: searching for %ld", bio->bi_sector);
    /* search the list in LRU order so single sequential flow hits first slot */
    VERIFY(spin_is_locked(&dmc->ioctl_lock));
    for (seqio = dmc->seq_io_head; seqio != NULL && sequential == 0; seqio = seqio->next) { 

        if (bio->bi_sector == seqio->most_recent_sector) {
            /* Reread or write same sector again.  Ignore but move to head */
            DPRINTK("skip_sequential_io: repeat");
            sequential = 1;
            if (dmc->seq_io_head != seqio)
                seq_io_move_to_lruhead(dmc, seqio);
        }
        /* i/o to one block more than the previous i/o = sequential */  
        else if (bio->bi_sector == seqio->most_recent_sector + dmc->block_size) {
            DPRINTK("skip_sequential_io: sequential found");
            /* Update stats.  */
            seqio->most_recent_sector = bio->bi_sector;
            seqio->sequential_count++;
            sequential = 1;

            /* And move to head, if not head already */
            if (dmc->seq_io_head != seqio)
                seq_io_move_to_lruhead(dmc, seqio);

            /* Is it now sequential enough to be sure? (threshold expressed in kb) */
            if (to_bytes(seqio->sequential_count * dmc->block_size) > dmc->sysctl_skip_seq_thresh_kb * 1024) {
                DPRINTK("skip_sequential_io: Sequential i/o detected, seq count now %lu", 
                    seqio->sequential_count);
                /* Sufficiently sequential */
                skip = 1;
            }
        }
    }
    if (!sequential) {
        /* Record the start of some new i/o, maybe we'll spot it as 
         * sequential soon.  */
        DPRINTK("skip_sequential_io: concluded that its random i/o");

        seqio = dmc->seq_io_tail;
        seq_io_move_to_lruhead(dmc, seqio);

        DPRINTK("skip_sequential_io: fill in data");

        /* Fill in data */
        seqio->most_recent_sector = bio->bi_sector;
        seqio->sequential_count   = 1;
    }
    DPRINTK("skip_sequential_io: complete.");
    if (skip) {
        if (bio_data_dir(bio) == READ)
                dmc->flashcache_stats.uncached_sequential_reads++;
        else 
                dmc->flashcache_stats.uncached_sequential_writes++;
    }

    return skip;
}






 struct thread_pool
{
    int                 thread_num;
    struct mutex        thread_lock;
    struct list_head    ready_list, active_list; 

    wait_queue_head_t   wait;
};


 struct privatedata
{
    char *name;
};


struct thread_pool_worker
{
    struct list_head    worker_entry;

    struct task_struct  *thread;

    struct thread_pool  *pool;

    int                 error;
    int                 has_data;
    int                 need_exit;
    unsigned int        id;

    wait_queue_head_t   wait;

    void                *private; 
    void                *schedule_data;

    int                 (* action)(void *private, void *schedule_data);
    void                (* cleanup)(void *private);
};

static struct privatedata *dmt;
static struct thread_pool *pool;
int count=0;

static void thread_pool_exit_worker(struct thread_pool_worker *w)
{
    kthread_stop(w->thread);

    w->cleanup(w->private);
    kfree(w);
}


static void thread_pool_worker_make_ready(struct thread_pool_worker *w)
{
    struct thread_pool *p = w->pool;

    mutex_lock(&p->thread_lock);

    if (!w->need_exit) {
        list_move_tail(&w->worker_entry, &p->ready_list);
        w->has_data = 0;
        mutex_unlock(&p->thread_lock);

        wake_up(&p->wait);
    } else {
        p->thread_num--;
        list_del(&w->worker_entry);
        mutex_unlock(&p->thread_lock);

        thread_pool_exit_worker(w);
    }
}


static int thread_pool_worker_func(void *data)
{
    struct thread_pool_worker *w = data;

    while (!kthread_should_stop()) {
        wait_event_interruptible(w->wait,kthread_should_stop() || w->has_data); 

        if (kthread_should_stop())
            break;

        if (!w->has_data)
            continue;

        w->action(w->private, w->schedule_data);
        thread_pool_worker_make_ready(w);
    }

    return 0;
}


void thread_pool_del_worker(struct thread_pool *p)
{
    struct thread_pool_worker *w = NULL;

    while (!w && p->thread_num) {
        wait_event(p->wait, !list_empty(&p->ready_list) || !p->thread_num);

        mutex_lock(&p->thread_lock);
        if (!list_empty(&p->ready_list)) {
            w = list_first_entry(&p->ready_list,
                    struct thread_pool_worker,
                    worker_entry);

            printk("%s: deleting w: %p, thread_num: %d, list: %p [%p.%p].\n",
                    __func__, w, p->thread_num, &p->ready_list,
                    p->ready_list.prev, p->ready_list.next);

            p->thread_num--;
            list_del(&w->worker_entry);
        }
        mutex_unlock(&p->thread_lock);
    }

    if (w)
        thread_pool_exit_worker(w);
    printk("%s: deleted w: %p, thread_num: %d.\n",
            __func__, w, p->thread_num);
}


void thread_pool_del_worker_id(struct thread_pool *p, unsigned int id)
{
    struct thread_pool_worker *w;
    int found = 0;

    mutex_lock(&p->thread_lock);
    list_for_each_entry(w, &p->ready_list, worker_entry) {
        if (w->id == id) {
            found = 1;
            p->thread_num--;
            list_del(&w->worker_entry);
            break;
        }
    }

    if (!found) {
        list_for_each_entry(w, &p->active_list, worker_entry) {
            if (w->id == id) {
                w->need_exit = 1;
                break;
            }
        }
    }
    mutex_unlock(&p->thread_lock);

    if (found)
        thread_pool_exit_worker(w);
}


int thread_pool_add_worker(struct thread_pool *p,
        char *name,
        unsigned int id,
        void *(* init)(void *private),
        void (* cleanup)(void *private),
        void *private)
{
    struct thread_pool_worker *w;
    int err = -ENOMEM;

    w = kzalloc(sizeof(struct thread_pool_worker), GFP_KERNEL);
    if (!w)
        goto err_out_exit;

    w->pool = p;
    init_waitqueue_head(&w->wait);
    w->cleanup = cleanup;
    w->id = id;

    w->thread = kthread_run(thread_pool_worker_func, w, "%s", name); 
    if (IS_ERR(w->thread)) {
        err = PTR_ERR(w->thread);
        goto err_out_free;
    }

    w->private = init(private);
    if (IS_ERR(w->private)) {
        err = PTR_ERR(w->private);
        goto err_out_stop_thread;
    }

    mutex_lock(&p->thread_lock);
    list_add_tail(&w->worker_entry, &p->ready_list); 
    p->thread_num++;
    mutex_unlock(&p->thread_lock);

    return 0;

err_out_stop_thread:
    kthread_stop(w->thread);
err_out_free:
    kfree(w);
err_out_exit:
    return err;
}


void thread_pool_destroy(struct thread_pool *p)
{
    while (p->thread_num) {
        printk("%s: num: %d.\n", __func__, p->thread_num);
        thread_pool_del_worker(p);
    }

    kfree(p);
}


struct thread_pool *thread_pool_create(int num, char *name,
        void *(* init)(void *private),
        void (* cleanup)(void *private),
        void *private)
{
    struct thread_pool_worker *w, *tmp;
    struct thread_pool *p;
    int err = -ENOMEM;
    int i;

    p = kzalloc(sizeof(struct thread_pool), GFP_KERNEL);
    if (!p)
        goto err_out_exit;

    init_waitqueue_head(&p->wait);
    mutex_init(&p->thread_lock);
    INIT_LIST_HEAD(&p->ready_list);
    INIT_LIST_HEAD(&p->active_list);
    p->thread_num = 0;

    for (i=0; i<num; ++i) {
        err = thread_pool_add_worker(p, name, i, init,
                cleanup, private);
        printk("%s: data: %p.\n", __func__);
        if (err)
            goto err_out_free_all;
    }

    return p;

err_out_free_all:
    list_for_each_entry_safe(w, tmp, &p->ready_list, worker_entry) {
        list_del(&w->worker_entry);
        thread_pool_exit_worker(w);
    }
    kfree(p);
err_out_exit:
    return ERR_PTR(err);
}




static inline void *private_init(void *data)
{
    printk("%s: data: %p.\n", __func__, data);
    return data;
}

static inline void private_cleanup(void *data)
{
    printk("%s: data: %p.\n", __func__, data);

}

int setup(void *private, void *data)
{
     printk("%s: data: %p.\n", __func__, data);
     return 0;
}

int action(void *private, void *data)
{ 
     count = count+1;
     printk("----------------------------------------------------------------------------------------------------------------action"+count);
     return 0;
}


int thread_pool_schedule_private(struct thread_pool *p,
        int (* setup)(void *private, void *data),
        int (* action)(void *private, void *data),
        void *data, long timeout, void *id)
{
    struct thread_pool_worker *w, *tmp, *worker = NULL;
    int err = 0;

    while (!worker && !err) {
        timeout = wait_event_interruptible_timeout(p->wait, !list_empty(&p->ready_list),timeout); 

        if (!timeout) {
            err = -ETIMEDOUT;
            break;
        }

        worker = NULL;
        mutex_lock(&p->thread_lock);
        list_for_each_entry_safe(w, tmp, &p->ready_list, worker_entry) { 
            if (id && id != w->private)
                continue;

            worker = w;

            list_move_tail(&w->worker_entry, &p->active_list);

            err = setup(w->private, data);
            if (!err) {
                w->schedule_data = data;
                w->action = action;
                w->has_data = 1;
                wake_up(&w->wait);
            } else {
                list_move_tail(&w->worker_entry, &p->ready_list);
            }

            break;
        }
        mutex_unlock(&p->thread_lock);
    }

    return err;
}

int thread_pool_schedule(struct thread_pool *p,
        int (* setup)(void *private, void *data),
        int (* action)(void *private, void *data),
        void *data, long timeout)
{
    return thread_pool_schedule_private(p, setup,
            action, data, timeout, NULL);
}


 int __init thread_pool_init(void)
{
    dmt= kzalloc(sizeof(struct privatedata), GFP_KERNEL);
    char *name=NULL;
    pool= thread_pool_create(10, name, private_init, private_cleanup, dmt);
    int n=300;
     while (n--)
      {
          mdelay(100);
          thread_pool_schedule_private(pool,setup,action, dmt, MAX_SCHEDULE_TIMEOUT,dmt);
      }
    return 0;
}

void __exit thread_pool_exit(void)
{
        thread_pool_destroy(pool);
}

module_init(thread_pool_init);
module_exit(thread_pool_exit);



