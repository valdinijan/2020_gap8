#include "rt/rt_api.h"
#include "stdio.h"

// MAT_DIM should be a multiple of NUM_CORES for below code to work
#define MAT_DIM 64
#define MAT_LEN ( MAT_DIM * MAT_DIM )
#define MAT_SIZE ( MAT_LEN * sizeof(unsigned short))
#define NUM_CORES 8

#define KERN_LEN 9
#define KERN_SIZE ( KERN_LEN * sizeof(unsigned short) )

static unsigned short l2_mat_in[MAT_DIM][MAT_DIM];
// static unsigned short l2_mat_in[MAT_DIM][MAT_DIM] =
// {
//   {1, 2, 0, 0},
//   {5, 3, 0, 4},
//   {0, 0, 0, 7},
//   {9, 3, 0, 0}
// };

static unsigned short l2_kernel[KERN_LEN] = {-1, -2, -1, 0, 0, 0, 1, 2, 1};
// static unsigned short l2_kernel[KERN_LEN] = {1, 1, 1};
static unsigned short l2_mat_conv[MAT_DIM][MAT_DIM];
static unsigned short l2_mat_conv_2[MAT_DIM][MAT_DIM];

#if 0
static void end_of_l2_to_l1_copy(void *arg)
{
  printf("Copied from L2 to L1\n");
}
#endif

static void cluster_core_single(void *arg)
{
  char * cluster_l1_buff = (char *) arg;

  printf("Entered core (%d)\n", rt_core_id());

  unsigned short * p_mat_in = (unsigned short *)&cluster_l1_buff[0];
  unsigned short * p_kernel = (unsigned short *)&cluster_l1_buff[MAT_SIZE];
  unsigned short * p_mat_conv = (unsigned short *)&cluster_l1_buff[MAT_SIZE + KERN_SIZE];
  int acc;
  int col_i;
  int val_i;

  for (int row = 0; row < MAT_DIM; row++)
  {
    for (int col = 0; col < MAT_DIM; col++)
    {
      acc = 0;
      col_i = col - (KERN_LEN / 2);
      // extend border valus if outside
      for (int kpos = 0; kpos < KERN_LEN; kpos++)
      {
        if (col_i < 0)
        {
          val_i = p_mat_in[row * MAT_DIM + 0];
        }
        else if (col_i >= KERN_LEN)
        {
          val_i = p_mat_in[row * MAT_DIM + MAT_DIM - 1];
        }
        else
        {
          val_i = p_mat_in[row * MAT_DIM + col_i];
        }

        acc += val_i * p_kernel[kpos];
        col_i++;
      }
      p_mat_conv[row * MAT_DIM + col] = acc;
    }
  }
}

static void cluster_core_parallel(void *arg)
{
  char * cluster_l1_buff = (char *) arg;

  printf("Entered core (%d)\n", rt_core_id());
  int start_row = rt_core_id() * MAT_DIM / NUM_CORES;
  int end_row = start_row + MAT_DIM / NUM_CORES - 1;

  unsigned short * p_mat_in = (unsigned short *)&cluster_l1_buff[0];
  unsigned short * p_kernel = (unsigned short *)&cluster_l1_buff[MAT_SIZE];
  unsigned short * p_mat_conv = (unsigned short *)&cluster_l1_buff[MAT_SIZE + KERN_SIZE + MAT_SIZE];
  int acc;
  int col_i;
  int val_i;

  for (int row = start_row; row < end_row; row++)
  {
    for (int col = 0; col < MAT_DIM; col++)
    {
      acc = 0;
      col_i = col - (KERN_LEN / 2);
      // extend border valus if outside
      for (int kpos = 0; kpos < KERN_LEN; kpos++)
      {
        if (col_i < 0)
        {
          val_i = p_mat_in[row * MAT_DIM + 0];
        }
        else if (col_i >= KERN_LEN)
        {
          val_i = p_mat_in[row * MAT_DIM + MAT_DIM - 1];
        }
        else
        {
          val_i = p_mat_in[row * MAT_DIM + col_i];
        }

        acc += val_i * p_kernel[kpos];
        col_i++;
      }
      p_mat_conv[row * MAT_DIM + col] = acc;
    }
  }
}

static void cluster_entry_single(void *arg)
{
  // Retrieve the pointer to the allocated memory
  char * cluster_l1_buff = (char *) arg;

  printf("(%d, %d) Entered cluster (sequential calculation)\n", rt_cluster_id(), rt_core_id());

  // Copy from L2 to shared L1
  rt_dma_copy_t dmaCp;
  rt_dma_memcpy((int)l2_mat_in, (int)cluster_l1_buff, MAT_SIZE, RT_DMA_DIR_EXT2LOC, 0, &dmaCp);
  // Wait for the operation to finish
  rt_dma_wait(&dmaCp);
  rt_dma_memcpy((int)l2_kernel, (int)&cluster_l1_buff[MAT_SIZE], KERN_SIZE, RT_DMA_DIR_EXT2LOC, 0, &dmaCp);
  // Wait for the operation to finish
  rt_dma_wait(&dmaCp);

  // Start all the cores up
  rt_team_fork(1, cluster_core_single, cluster_l1_buff);

  // Copy result from shared L1 to L2
  rt_dma_memcpy((int)l2_mat_conv, (int)&cluster_l1_buff[MAT_SIZE + KERN_SIZE],
                MAT_SIZE, RT_DMA_DIR_LOC2EXT, 0, &dmaCp);
  // Wait for it to finish
  rt_dma_wait(&dmaCp);
}

static void cluster_entry_parallel(void *arg)
{
  // Retrieve the pointer to the allocated memory
  char * cluster_l1_buff = (char *) arg;

  printf("(%d, %d) Entered cluster (parallel calculation)\n", rt_cluster_id(), rt_core_id());

  // Copy from L2 to shared L1
  rt_dma_copy_t dmaCp;
  rt_dma_memcpy((int)l2_mat_in, (int)cluster_l1_buff, MAT_SIZE, RT_DMA_DIR_EXT2LOC, 0, &dmaCp);
  // Wait for the operation to finish
  rt_dma_wait(&dmaCp);
  rt_dma_memcpy((int)l2_kernel, (int)&cluster_l1_buff[MAT_SIZE], KERN_SIZE, RT_DMA_DIR_EXT2LOC, 0, &dmaCp);
  // Wait for the operation to finish
  rt_dma_wait(&dmaCp);

  // Start all the cores up
  rt_team_fork(NUM_CORES, cluster_core_parallel, cluster_l1_buff);

  // Copy result from shared L1 to L2
  rt_dma_memcpy((int)l2_mat_conv_2, (int)&cluster_l1_buff[MAT_SIZE + KERN_SIZE],
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
  for (int row = 0; row < MAT_DIM; row++)
  {
    for (int col = 0; col < MAT_DIM; col++)
    {
      l2_mat_in[row][col] = row * MAT_DIM + col; 
    }
  }

  // Power on the cluster
  rt_cluster_mount(1, 0, 0, NULL);

  // Allocate a buffer in the shared L1 memory
  char * cluster_l1_buff = rt_alloc(RT_ALLOC_CL_DATA, MAT_SIZE * 4);

// Copy by fabric controller seems not supported by GVSOC
#if 0
  // Copy from L2 to L1
  rt_event_t *p_event = rt_event_get(NULL, end_of_l2_to_l1_copy, (void *)0);
  rt_dma_copy_t dmaCp;
  rt_dma_memcpy_event((int)l2_mat_buf, (int)cluster_l1_buff, MAT_LEN * 2, RT_DMA_DIR_EXT2LOC, &dmaCp, p_event);
  // Wait for copy to finish
  rt_event_execute(NULL, 1);
#endif

  // Call the cluster with the buffer address as an argument. Block until finished.
  rt_cluster_call(NULL, 0, cluster_entry_single, cluster_l1_buff, NULL, 0, 0, rt_nb_pe(), NULL);
  // Call the cluster with the buffer address as an argument. Block until finished.
  rt_cluster_call(NULL, 0, cluster_entry_parallel, cluster_l1_buff, NULL, 0, 0, rt_nb_pe(), NULL);

  // Free the buffer
  rt_free(RT_ALLOC_CL_DATA, cluster_l1_buff, MAT_SIZE * 3);

  // Switch off the cluster
  rt_cluster_mount(0, 0, 0, NULL);
  printf("Cluster switched off\n");

#if 0
  printf("Sequential result:\n");
  for (int row = 0; row < MAT_DIM; row++)
  {
    for (int col = 0; col < MAT_DIM; col++)
    {
      printf("conv (%d, %d): %d\n", row, col, l2_mat_conv[row][col]);
    }
  }

  printf("Parallel result:\n");
  for (int row = 0; row < MAT_DIM; row++)
  {
    for (int col = 0; col < MAT_DIM; col++)
    {
      printf("conv (%d, %d): %d\n", row, col, l2_mat_conv_2[row][col]);
    }
  }
#endif

  printf("Verifying seq vs parallel...\n");
  for (int row = 0; row < MAT_DIM; row++)
  {
    for (int col = 0; col < MAT_DIM; col++)
    {
      if (l2_mat_conv[row][col] != l2_mat_conv_2[row][col])
      {
        printf("Verification failed at (%d, %d): %d, %d\n", row, col, l2_mat_conv[row][col], l2_mat_conv_2[row][col]);
        return -1;
      }
    }
  }
  printf("Verification passed.\n");
}
