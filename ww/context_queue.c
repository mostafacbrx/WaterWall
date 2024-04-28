#include "context_queue.h"

#define i_TYPE queue, context_t * // NOLINT
#include "stc/deq.h"
#define Q_CAP 25

struct context_queue_s
{
    queue          q;
    buffer_pool_t *pool;
};

context_queue_t *newContextQueue(buffer_pool_t *pool)
{
    context_queue_t *cb = malloc(sizeof(context_queue_t));
    cb->q               = queue_with_capacity(Q_CAP);
    cb->pool            = pool;
    return cb;
}
void destroyContextQueue(context_queue_t *self)
{
    c_foreach(i, queue, self->q)
    {
        if ((*i.ref)->payload != NULL)
        {
            reuseBuffer(self->pool, (*i.ref)->payload);
            (*i.ref)->payload = NULL;
        }
        destroyContext((*i.ref));
    }

    queue_drop(&self->q);
    free(self);
}

void contextQueuePush(context_queue_t *self, context_t *context)
{
    if (context->src_io)
    {
        context->fd = hio_fd(context->src_io);
    }
    queue_push_back(&self->q, context);
}

// todo (better way) probably blacklisting the last closed io can solve this problem and it has much better performance
context_t *contextQueuePop(context_queue_t *self)
{
    context_t *context = queue_pull_front(&self->q);

    // if (context->fd == 0 || ! hio_exists(context->line->loop, context->fd))
    // {
    //     context->src_io = NULL;
    // }
    // else
    // {
    //     if (hio_is_closed(context->src_io) || context->src_io != hio_get(context->line->loop, context->fd))
    //     {
    //         context->src_io = NULL;
    //     }
    // }

    return context;
}
size_t contextQueueLen(context_queue_t *self)
{
    return queue_size(&self->q);
}

void contextQueueNotifyIoRemoved(context_queue_t *self, hio_t *io)
{
    if (io == NULL)
    {
        return;
    }
    c_foreach(i, queue, self->q)
    {
        if ((*i.ref)->src_io == io)
        {
            (*i.ref)->src_io = NULL;
        }
        destroyContext((*i.ref));
    }
}
