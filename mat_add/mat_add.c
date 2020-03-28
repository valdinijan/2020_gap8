#include "rt/rt_api.h"
#include "stdio.h"

// MAT_DIM must be a multiple of 4 for below code to work
#define MAT_DIM 64
#define MAT_1D_LEN ( MAT_DIM * MAT_DIM )
#define MAT_SIZE ( MAT_1D_LEN * sizeof(unsigned short))

static unsigned short l2_mat_buf[MAT_1D_LEN * 3];
unsigned short * p_l2_mat_1 = &l2_mat_buf[0];
unsigned short * p_l2_mat_2 = &l2_mat_buf[MAT_1D_LEN];
unsigned short * p_l2_mat_r = &l2_mat_buf[MAT_1D_LEN * 2];

static unsigned short l2_mat_verif[MAT_1D_LEN];

#if 0
static void end_of_l2_to_l1_copy(void *arg)
{
  printf("Copied from L2 to L1\n");
}
#endif

static void single_core_work(void *arg)
{
  char * cluster_l1_buff = (char *) arg;

  int mat_1_start = rt_core_id() * MAT_SIZE / 8;
  int mat_2_start = (rt_core_id() * MAT_SIZE / 8) + MAT_SIZE;
  int mat_r_start = (rt_core_id() * MAT_SIZE / 8) + (MAT_SIZE * 2);
  unsigned short * p_mat_1 = (unsigned short *)&cluster_l1_buff[mat_1_start];
  unsigned short * p_mat_2 = (unsigned short *)&cluster_l1_buff[mat_2_start];
  unsigned short * p_mat_r = (unsigned short *)&cluster_l1_buff[mat_r_start];

  int chunk_len = MAT_1D_LEN / 8;
  printf("(%d, %d): start_1 %d, start_2 %d\n", rt_cluster_id(), rt_core_id(), mat_1_start, mat_2_start);

  for (int offs = 0; offs < chunk_len; offs++)
  {
    *p_mat_r = *p_mat_1 + *p_mat_2;
    p_mat_1++;
    p_mat_2++;
    p_mat_r++;
  }
}

static void cluster_entry(void *arg)
{
  // Retrieve the pointer to the allocated memory
  char * cluster_l1_buff = (char *) arg;

  printf("(%d, %d) Entered cluster\n", rt_cluster_id(), rt_core_id());

  // Copy from L2 to shared L1
  rt_dma_copy_t dmaCp;
  rt_dma_memcpy((int)l2_mat_buf, (int)cluster_l1_buff, MAT_SIZE * 3, RT_DMA_DIR_EXT2LOC, 0, &dmaCp);
  // Wait for the operation to finish
  rt_dma_wait(&dmaCp);

  // Start all the cores up
  rt_team_fork(8, single_core_work, cluster_l1_buff);

  // Copy result from shared L1 to L2
  rt_dma_memcpy((int)p_l2_mat_r, (int)&cluster_l1_buff[MAT_SIZE * 2],
                MAT_SIZE, RT_DMA_DIR_LOC2EXT, 0, &dmaCp);
  // Wait for it to finish
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
  char * cluster_l1_buff = rt_alloc(RT_ALLOC_CL_DATA, MAT_SIZE * 3);

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
  rt_free(RT_ALLOC_CL_DATA, cluster_l1_buff, MAT_SIZE * 3);

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
      return -1;
    }
  }
  printf("OK.\n");
}
