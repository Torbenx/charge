#!/bin/bash

# name of the FIFOs
FIFO_PREFIX="perf_fd"

# remove dangling files if any
rm -rf ${FIFO_PREFIX}.*

# create two fifos
mkfifo ${FIFO_PREFIX}.ctl
mkfifo ${FIFO_PREFIX}.ack

# associate file descriptors
exec {perf_ctl_fd}<>${FIFO_PREFIX}.ctl
exec {perf_ack_fd}<>${FIFO_PREFIX}.ack

# set env vars for application
export PERF_CTL_FD=${perf_ctl_fd}
export PERF_ACK_FD=${perf_ack_fd}

# start perf with the associated file descriptors
perf stat \
    --event=cycles,instructions,branches,branch-misses \
    --delay=-1 --repeat=100 \
    --control fd:${perf_ctl_fd},${perf_ack_fd} \
    -- ./build/charge "$@" > /dev/null

unlink ${FIFO_PREFIX}.ctl
unlink ${FIFO_PREFIX}.ack