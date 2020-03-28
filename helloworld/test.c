/*
 * Copyright (C) 2017 ETH Zurich, University of Bologna and GreenWaves Technologies
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms
 * of the BSD license.  See the LICENSE file for details.
 */

#include "rt/rt_api.h"

#define STACK_SIZE      2048
#define MOUNT           1
#define UNMOUNT         0
#define CID             0

unsigned int done = 0;
#ifdef PRINTF_UART
unsigned int __rt_iodev=RT_IODEV_UART;
#endif

static void hello(void *arg)
{
  printf("hello world [0x%02x, %d]\n", rt_cluster_id(), rt_core_id());
}

static void cluster_entry(void *arg)
{
  printf("Entering cluster on core %d\n", rt_core_id());
  printf("There are %d cores available here.\n", rt_nb_pe());
  // executed on master core (0)
  // fork 8 threads that are then dispatched to 8 cores by the OS
  rt_team_fork(8, hello, (void *)0x0);
  printf("Leaving cluster on core %d\n", rt_core_id());
}

static void end_of_call(void *arg)
{
  // called back after cluster jobs are completed
  // executed on fabric controller
  printf("hello world [0x%02x, %d]\n", rt_cluster_id(), rt_core_id());
  done = 1;
}

int main()
{
  printf("Entering main controller\n");

  // allocate 4 events and put them on the free list
  if (rt_event_alloc(NULL, 4)) return -1;

  // initialize one event on default scheduler
  rt_event_t *p_event = rt_event_get(NULL, end_of_call, (void *) CID);

  // power up the cluster
  rt_cluster_mount(MOUNT, CID, 0, NULL);

  // enqueue job for the cluster
  // allow up to 8 cores
  // register event to be called back when finished
  rt_cluster_call(NULL, CID, cluster_entry, NULL, NULL, 0, 0, rt_nb_pe(), p_event);

  while(!done)
      // blocking wait on default scheduler
      rt_event_execute(NULL, 1);

  rt_cluster_mount(UNMOUNT, CID, 0, NULL);

  printf("Test success: Leaving main controller\n");
  return 0;
}
