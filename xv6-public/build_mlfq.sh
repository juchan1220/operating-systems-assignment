#!/bin/bash

make clean
make SCHED_POLICY=MLFQ_SCHED MLFQ_K=5
make fs.img SCHED_POLICY=MLFQ_SCHED MLFQ_K=5 