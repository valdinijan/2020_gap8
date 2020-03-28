#include "rt/rt_api.h"
#include "stdio.h"

#define MAT_DIM 4
#define MAT_1D_LEN ( MAT_DIM * MAT_DIM )

static unsigned short l2_mat_buf[MAT_1D_LEN * 3];
unsigned short * p_l2_mat_1 = &l2_mat_buf[0];
unsigned short * p_l2_mat_2 = &l2_mat_buf[MAT_1D_LEN];
unsigned short * p_l2_mat_r = &l2_mat_buf[MAT_1D_LEN * 2];

static unsigned short l2_mat_verif[MAT_1D_LEN];

static void end_of_l2_to_l1_copy(void *arg)
{
  printf("Copied from L2 to L1\n");
}

static void cluster_entry(void *arg)
{
  // Retrieve the pointer to the allocated memory
  char * cluster_l1_buff = (char *) arg;

  printf("(%d, %d) Entered cluster\n", rt_cluster_id(), rt_core_id());

  // Copy from L2 to shared L1
  rt_dma_copy_t dmaCp;
  rt_dma_memcpy((int)l2_mat_buf, (int)cluster_l1_buff, MAT_1D_LEN * 2, RT_DMA_DIR_EXT2LOC, 0, &dmaCp);
  // Wait for the operation to finish
  rt_dma_wait(&dmaCp);

}

int main()
{
  printf("Entering main controller\n");

  // Allocate events on the default scheduler
  if (rt_event_alloc(NULL, 4)) return -1;

  // Initialize matrices in L2 memory
  for (int i = 0; i < MAT_1D_LEN; i++)
  {
    p_l2_mat_1[i] = i;
    p_l2_mat_2[i] = MAT_1D_LEN + i;
    p_l2_mat_r[i] = 0;
    l2_mat_verif[i] = i + MAT_1D_LEN + i;
  }

  // Power on the cluster
  rt_cluster_mount(1, 0, 0, NULL);

  // Allocate a buffer in the shared L1 memory
  char * cluster_l1_buff = rt_alloc(RT_ALLOC_CL_DATA, MAT_1D_LEN * 3);

// Copy by fabric controller seems not supported by GVSOC
#if 0
  // Copy from L2 to L1
  rt_event_t *p_event = rt_event_get(NULL, end_of_l2_to_l1_copy, (void *)0);
  rt_dma_copy_t dmaCp;
  rt_dma_memcpy_event((int)l2_mat_buf, (int)cluster_l1_buff, MAT_1D_LEN * 2, RT_DMA_DIR_EXT2LOC, &dmaCp, p_event);
  // Wait for copy to finish
  rt_event_execute(NULL, 1);
#endif

  // Call the cluster with the buffer address as an argument. Block until finished.
  rt_cluster_call(NULL, 0, cluster_entry, cluster_l1_buff, NULL, 0, 0, rt_nb_pe(), NULL);

  // Free the buffer
  rt_free(RT_ALLOC_CL_DATA, cluster_l1_buff, MAT_1D_LEN * 3);

  // Switch off the cluster
  rt_cluster_mount(0, 0, 0, NULL);
  printf("Cluster switched off\n");

  // Verify correct addition
  printf("Verifying...\n");
  for (int i = 0; i < MAT_1D_LEN; i++)
  {
    if (p_l2_mat_r[i] != l2_mat_verif[i])
    {
      printf("Test failed: ERROR at index %d: expected %d, actual %d\n",
             i,
             l2_mat_verif[i],
             p_l2_mat_r[i]);
    }
  }
}
