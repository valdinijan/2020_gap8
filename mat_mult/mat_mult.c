#include "rt/rt_api.h"
#include "stdio.h"

// MAT_DIM must be a multiple of 8 for below code to work
#define MAT_DIM 64
#define MAT_LEN ( MAT_DIM * MAT_DIM )
#define MAT_SIZE ( MAT_LEN * sizeof(unsigned short))
#define NUM_CORES 8

#define KERN_LEN 9
#define KERN_SIZE ( KERN_LEN * sizeof(unsigned short) )

#define MAT_NUM 4

static unsigned short l2_mat[MAT_NUM][MAT_LEN];

#if 0
static void end_of_l2_to_l1_copy(void *arg)
{
  printf("Copied from L2 to L1\n");
}
#endif

static void single_core_mult(void *arg)
{
  char * cluster_l1_buff = (char *) arg;

  unsigned short * p_mat_1 = (unsigned short *)&cluster_l1_buff[0];
  unsigned short * p_mat_2 = (unsigned short *)&cluster_l1_buff[MAT_SIZE];
  unsigned short * p_mat_r = (unsigned short *)&cluster_l1_buff[MAT_SIZE * 2];
  int acc;
  unsigned short * p_mat_1_pos;
  unsigned short * p_mat_2_pos;

  // divide job by rows of matrix 1
  // start_pos: first element of the starting row for this core
  int mat_1_row_start = (MAT_DIM / NUM_CORES) * MAT_DIM * rt_core_id();
  printf("(%d, %d): mat_1_row_start %d\n", rt_cluster_id(), rt_core_id(), mat_1_row_start);

  // iterate over mat_1 rows that assigned to this core
  for (int row = 0; row < (MAT_DIM / NUM_CORES); row++)
  {
    // iterate over mat_2 columns
    for (int j = 0; j < MAT_DIM; j++)
    {
      // return to start of mat_1 row
      p_mat_1_pos = &p_mat_1[mat_1_row_start];
      // select mat_2 column
      p_mat_2_pos = &p_mat_2[j];
      // multiply mat_1 row with mat_2 column
      acc = 0;
      for (int i = 0; i < MAT_DIM; i++)
      {
        acc += (*p_mat_1_pos * *p_mat_2_pos);
        p_mat_1_pos++;
        p_mat_2_pos += MAT_DIM;
      }
      // store in mat_r
      p_mat_r[mat_1_row_start + j] = acc;
    }
    // move to next mat_1 row
    mat_1_row_start += MAT_DIM;
  }
}

static void single_core_conv(void *arg)
{
  char * cluster_l1_buff = (char *) arg;

  printf("Entered core (%d)\n", rt_core_id());
  unsigned short conv_kernel[KERN_LEN] = {-1, -2, -1, 0, 0, 0, 1, 2, 1};

  int start_row = rt_core_id() * MAT_DIM / NUM_CORES;
  int end_row = start_row + MAT_DIM / NUM_CORES - 1;

  unsigned short * p_mat_in = (unsigned short *)&cluster_l1_buff[MAT_SIZE * 2];
  unsigned short * p_mat_conv = (unsigned short *)&cluster_l1_buff[MAT_SIZE * 3];
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

        acc += val_i * conv_kernel[kpos];
        col_i++;
      }
      p_mat_conv[row * MAT_DIM + col] = acc;
    }
  }
}

static void cluster_entry(void *arg)
{
  // Retrieve the pointer to the allocated memory
  char * cluster_l1_buff = (char *) arg;

  printf("(%d, %d) Entered cluster\n", rt_cluster_id(), rt_core_id());

  // Copy from L2 to shared L1
  rt_dma_copy_t dmaCp;
  rt_dma_memcpy((int)l2_mat, (int)cluster_l1_buff, MAT_SIZE * MAT_NUM, RT_DMA_DIR_EXT2LOC, 0, &dmaCp);
  // Wait for the operation to finish
  rt_dma_wait(&dmaCp);

  // Start all the cores up
  rt_team_fork(NUM_CORES, single_core_mult, cluster_l1_buff);

  // Copy result from shared L1 to L2
  rt_dma_memcpy((int)l2_mat[2], (int)&cluster_l1_buff[MAT_SIZE * 2],
                MAT_SIZE, RT_DMA_DIR_LOC2EXT, 0, &dmaCp);
  // Wait for it to finish
  rt_dma_wait(&dmaCp);
}

static void cluster_entry_conv(void *arg)
{
  // Retrieve the pointer to the allocated memory
  char * cluster_l1_buff = (char *) arg;

  printf("(%d, %d) Entered cluster\n", rt_cluster_id(), rt_core_id());

  // Start all the cores up
  rt_team_fork(NUM_CORES, single_core_conv, cluster_l1_buff);

  // Copy result from shared L1 to L2
  rt_dma_copy_t dmaCp;
  rt_dma_memcpy((int)l2_mat[3], (int)&cluster_l1_buff[MAT_SIZE * 3],
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
  for (int i = 0; i < MAT_LEN; i++)
  {
    l2_mat[0][i] = i;
    l2_mat[1][i] = 1;
    l2_mat[2][i] = 0;
    l2_mat[3][i] = 0;
  }

  // set identiny matrix for mat_2
  for (int i = 0; i < MAT_DIM; i++)
  {
    for (int j = 0; j < MAT_DIM; j++)
    {
      if (i == j)
        l2_mat[1][i * MAT_DIM + j] = 1;
      else
        l2_mat[1][i * MAT_DIM + j] = 0;
    }
  }

  // Power on the cluster
  rt_cluster_mount(1, 0, 0, NULL);

  // Allocate a buffer in the shared L1 memory
  char * cluster_l1_buff = rt_alloc(RT_ALLOC_CL_DATA, MAT_SIZE * MAT_NUM);

// Copy by fabric controller seems not supported by GVSOC
#if 0
  // Copy from L2 to L1
  rt_event_t *p_event = rt_event_get(NULL, end_of_l2_to_l1_copy, (void *)0);
  rt_dma_copy_t dmaCp;
  rt_dma_memcpy_event((int)l2_mat_buf, (int)cluster_l1_buff, MAT_LEN * 2, RT_DMA_DIR_EXT2LOC, &dmaCp, p_event);
  // Wait for copy to finish
  rt_event_execute(NULL, 1);
#endif

  // Call the cluster: parallel multiplication
  rt_cluster_call(NULL, 0, cluster_entry, cluster_l1_buff, NULL, 0, 0, rt_nb_pe(), NULL);

  // Call the cluster: parallel convolution
  rt_cluster_call(NULL, 0, cluster_entry_conv, cluster_l1_buff, NULL, 0, 0, rt_nb_pe(), NULL);

  // Free the buffer
  rt_free(RT_ALLOC_CL_DATA, cluster_l1_buff, MAT_SIZE * MAT_NUM);

  // Switch off the cluster
  rt_cluster_mount(0, 0, 0, NULL);
  printf("Cluster switched off\n");

#if 0
  // Dump result
  printf("Result dump...\n");
  for (int i = 0; i < MAT_LEN; i++)
  {
    printf("mat_r[%d]: %d\n", i, l2_mat[3][i]);
  }
  printf("OK.\n");
#endif
}
