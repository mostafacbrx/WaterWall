#pragma once

#include "tunnel.h"
#include "hmutex.h"

/*

    pipeline helps you connect 2 lines that are not in the same thread/worker

    it manages the thread safety and efficiency

    i hope you don't use it, currently only used for halfduplex server since there were no other way...

*/

struct pipe_line_s
{
    atomic_bool closed;
    atomic_int  refc;

    // thread local:
    tunnel_t *self;
    uint8_t   left_tid;
    uint8_t   right_tid;
    line_t   *left_line;
    line_t   *right_line;

    TunnelFlowRoutine local_up_stream;
    TunnelFlowRoutine local_down_stream;
};

typedef struct pipe_line_s pipe_line_t;

bool writePipeLineLTR(pipe_line_t *pl, context_t *c);
bool writePipeLineRTL(pipe_line_t *pl, context_t *c);

pipe_line_t *newPipeLineLeft(uint8_t tid_left, line_t *left_line, uint8_t tid_right, line_t *right_line,
                             TunnelFlowRoutine local_up_stream, TunnelFlowRoutine local_down_stream);
pipe_line_t *newPipeLineRight(uint8_t tid_left, line_t *left_line, uint8_t tid_right, line_t *right_line,
                              TunnelFlowRoutine local_up_stream, TunnelFlowRoutine local_down_stream);