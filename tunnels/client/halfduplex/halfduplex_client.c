#include "halfduplex_client.h"
#include "buffer_pool.h"
#include "frand.h"
#include "halfduplex_constants.h"
#include "shiftbuffer.h"
#include "tunnel.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct halfduplex_state_s
{
    void *_;
} halfduplex_state_t;

typedef struct halfduplex_con_state_s
{
    line_t *main_line;
    line_t *upload_line;
    line_t *download_line;

} halfduplex_con_state_t;

static void cleanup(halfduplex_con_state_t *cstate)
{

    if (cstate->upload_line)
    {
        doneLineDownSide(cstate->upload_line);
        destroyLine(cstate->upload_line);
    }
    if (cstate->download_line)
    {
        doneLineDownSide(cstate->download_line);
        destroyLine(cstate->download_line);
    }
    doneLineUpSide(cstate->main_line);
    free(cstate);
}

static void onMainLinePaused(void *cstate)
{
    pauseLineUpSide(((halfduplex_con_state_t *) cstate)->upload_line);
    pauseLineUpSide(((halfduplex_con_state_t *) cstate)->download_line);
}

static void onMainLineResumed(void *cstate)
{
    resumeLineUpSide(((halfduplex_con_state_t *) cstate)->upload_line);
    resumeLineUpSide(((halfduplex_con_state_t *) cstate)->download_line);
}
static void onUDLinePaused(void *cstate)
{
    pauseLineDownSide(((halfduplex_con_state_t *) cstate)->main_line);
}

static void onUDLineResumed(void *cstate)
{
    resumeLineDownSide(((halfduplex_con_state_t *) cstate)->main_line);
}

static void upStream(tunnel_t *self, context_t *c)
{
    halfduplex_con_state_t *cstate = CSTATE(c);
    if (c->payload != NULL)
    {
        if (c->first)
        {
            // 63 bits of random is enough and is better than hashing sender addr on halfduplex server, i believe so...
            uint32_t cids[2] = {fastRand(), fastRand()};

            context_t *intro_context = newContext(cstate->download_line);
            intro_context->first     = true;
            intro_context->payload   = popBuffer(getContextBufferPool(c));

            cids[0] = htonl(cids[0] | (1 << 31)); // kHLFDCmdDownload
            shiftl(intro_context->payload, 16);
            writeRaw(intro_context->payload, &cids[0], sizeof(cids));

            // shiftl(intro_context->payload, 1);
            // writeUI8(intro_context->payload, kHLFDCmdDownload);

            self->up->upStream(self->up, intro_context);

            if (! isAlive(c->line))
            {
                reuseContextBuffer(c);
                destroyContext(c);
                return;
            }

            cids[0] =  htonl(cids[0] & 0x7FFFFFFF); // kHLFDCmdUpload
            shiftl(intro_context->payload, 16);
            writeRaw(intro_context->payload, &cids[0], sizeof(cids));

            // shiftl(intro_context->payload, 1);
            // writeUI8(intro_context->payload, kHLFDCmdUpload);
        }
        self->up->upStream(self->up, switchLine(c, cstate->upload_line));
    }
    else
    {

        if (c->init)
        {
            halfduplex_con_state_t *cstate = malloc(sizeof(halfduplex_con_state_t));

            *cstate = (halfduplex_con_state_t){
                .download_line = newLine(c->line->tid), .upload_line = newLine(c->line->tid), .main_line = c->line};

            CSTATE_MUT(c)                     = cstate;
            LSTATE_MUT(cstate->upload_line)   = cstate;
            LSTATE_MUT(cstate->download_line) = cstate;

            lockLine(cstate->upload_line);
            self->up->upStream(self->up, newInitContext(cstate->upload_line));

            if (! isAlive(cstate->upload_line))
            {
                unLockLine(cstate->upload_line);

                CSTATE_MUT(c)                     = NULL;
                LSTATE_MUT(cstate->upload_line)   = NULL;
                LSTATE_MUT(cstate->download_line) = NULL;
                cleanup(cstate);

                self->dw->downStream(self->dw, newFinContextFrom(c));
                destroyContext(c);
                return;
            }
            unLockLine(cstate->upload_line);

            lockLine(cstate->download_line);
            self->up->upStream(self->up, newInitContext(cstate->download_line));

            if (! isAlive(cstate->download_line))
            {
                unLockLine(cstate->download_line);
                self->up->upStream(self->up, newFinContext(cstate->upload_line));

                CSTATE_MUT(c)                     = NULL;
                LSTATE_MUT(cstate->upload_line)   = NULL;
                LSTATE_MUT(cstate->download_line) = NULL;
                cleanup(cstate);

                self->dw->downStream(self->dw, newFinContextFrom(c));
                destroyContext(c);
                return;
            }
            unLockLine(cstate->download_line);
            setupLineUpSide(cstate->main_line, onMainLinePaused, cstate, onMainLineResumed);
            setupLineDownSide(cstate->upload_line, onUDLinePaused, cstate, onUDLineResumed);
            setupLineDownSide(cstate->download_line, onUDLinePaused, cstate, onUDLineResumed);
            destroyContext(c);
        }
        else if (c->fin)
        {
            self->up->upStream(self->up, newFinContext(cstate->upload_line));
            self->up->upStream(self->up, newFinContext(cstate->download_line));

            CSTATE_MUT(c)                     = NULL;
            LSTATE_MUT(cstate->upload_line)   = NULL;
            LSTATE_MUT(cstate->download_line) = NULL;
            cleanup(cstate);

            destroyContext(c);
        }
    }
}
static void downStream(tunnel_t *self, context_t *c)
{
    halfduplex_con_state_t *cstate = CSTATE(c);
    if (c->payload != NULL)
    {
        self->dw->downStream(self->dw, switchLine(c, cstate->main_line));
    }
    else
    {

        if (c->fin)
        {
            if (c->line == cstate->download_line)
            {
                self->up->upStream(self->up, newFinContext(cstate->upload_line));
                self->dw->downStream(self->dw, newFinContext(cstate->main_line));
            }
            else
            {
                assert(c->line == cstate->upload_line);
                self->up->upStream(self->up, newFinContext(cstate->download_line));
                self->dw->downStream(self->dw, newFinContext(cstate->main_line));
            }

            CSTATE_MUT(c)                     = NULL;
            LSTATE_MUT(cstate->upload_line)   = NULL;
            LSTATE_MUT(cstate->download_line) = NULL;
            cleanup(cstate);
            destroyContext(c);
        }
        else
        {
            if (c->line == cstate->download_line)
            {
                self->dw->downStream(self->dw, switchLine(c, cstate->main_line));
            }
            else
            {
                destroyContext(c);
            }
        }
    }
}

tunnel_t *newHalfDuplexClient(node_instance_context_t *instance_info)
{
    (void) instance_info;
    halfduplex_state_t *state = malloc(sizeof(halfduplex_state_t));
    memset(state, 0, sizeof(halfduplex_state_t));

    tunnel_t *t   = newTunnel();
    t->state      = state;
    t->upStream   = &upStream;
    t->downStream = &downStream;

    return t;
}

api_result_t apiHalfDuplexClient(tunnel_t *self, const char *msg)
{
    (void) (self);
    (void) (msg);
    return (api_result_t){0};
}

tunnel_t *destroyHalfDuplexClient(tunnel_t *self)
{
    (void) (self);
    return NULL;
}
tunnel_metadata_t getMetadataHalfDuplexClient(void)
{
    return (tunnel_metadata_t){.version = 0001, .flags = 0x0};
}
