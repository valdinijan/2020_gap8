# 2020_gap8
GAP8 test project

## i : Helloworld

Helloworld written by looking at provided example for PULP-OS. Some comments added to try to show understanding of the code.

## iv

* Assumptions about the problem:
  - VGA grayscale 640x480, 8bpp, @30Hz => 300kB per frame
  - 4kB reserved for detection output (a random guess)

### iv option 1 (real time, no RAM, switching the cluster) 

* Memory organization:
  - cluster L1: 
    - 1 block of 32kB (input); each core is processing its 1/8 of the image segment
    - 1 block of 4kB (output) to contain detection output, if any
  - L2:
    - 2 x 32kB buffers to store image semgent from the camera driver
    - 1 x 4kB buffer to store cluster algorithm output

* Operation (without storing image in RAM):
  - assuming clusters are able to process the feed in real time

  - FC:
    - FC (by blocking read) asks camera driver for next 32kB image segment; segment appears in L2
    - FC copies 32kB segment to cluster L1
    - FC calls cluster to process the image segment
    - waits for cluster to finish
    - shuts down the cluster
    - copies algorithm output to RAM
    - repeat

  - Cluster:
    - gets called by FC, does processing of 32kB image segment
    - writes result to output buffer in L1
    - copies output from L1 to L2

### iv option 2 (real time, no RAM, always-on cluster)

* Memory organization:
  - cluster L1: 
    - 2 x 16kB (input); double buffer; each core is processing its 1/8 of a buffer, the other buffer is being written by FC with next segment
    - 1 block of 4kB (output) to contain detection output, if any
  - L2:
    - 2 x 32kB buffers to store image semgent from the camera driver
    - 1 x 4kB buffer to store cluster algorithm output

* Operation:
  - FC:
    1. FC starts the cluster (cluster stays always on)
    2. FC waits on either camera data or on cluster finish event
    3. FC copies next 16kB segment to ping/pong L1 buffer
    4. goes back to 2.

  - Cluster:
    1. Checks/waits to a 16k buffer is ready, then processes it
    2. Toggles active buffer, then goes to 1

### iv option 3 (storing a full frame in RAM, then skipping frames):
  - assuming the clusters are not able to process the feed in real time
  - then the plan could be to store a full frame in RAM, and skip further frames until this one is processed

* Memory organization:
  - L2:
    - 2 x 64kB double buffer to receive from camera and send to RAM
    - then, 2 x 16kB to read from RAM and send to cluster L1
  - Cluster L1:
    - 2 x 16kB double buffer to receive from L2 and process

* Operation (with storing image in RAM):
  - FC:
    1. FC (by blocking read) asks camera driver for next 64kB image segment; segment appears in L2
    2. FC writes the segment to RAM
    3. Repeat 1-2 until entire frame is in RAM
    4. Power up the cluster and call it
    5. Read from RAM in 2 x 16kB double buffer and send to cluster L1 until full frame is processed

  - Cluster:
    1. Checks/waits to a 16k buffer is ready, then processes it
    2. Toggles active buffer, then goes to 1

## v : GAP8 I2S driver

The driver receives input PCM stream by using uDMA and stores it in buffers provided by user.
Minimum of 2 buffers are used, so that receiving can continue while user is reading previous buffer from his side. 2 buffer modes are supported: ping-pong and ring buffer of arbitrary size.
A blocking read is provided to the user. PMSIS task support is used to queue and wake up waiters.
Brief description of driver functions is given below.

* pi_i2s_conf_init()
  Initialize interface configuration to default values

* pi_i2s_open()
  - apply user conf to driver:
    - configure DMA channel and callback event
    - configure data format (PDM vs PCM)
    - for slab mode, allocate ring buffer in FC L1
    - for PDM mode, configure PDM shift
    - for PCM mode, configure bit frequency

* pi_i2s_ioctl()
  - start/stop (resume/suspend)

* pos_i2s_resume()
  - enqueue 2 buffers to be used by uDMA to do transfer from I2S to L2
  - start I2S clock

* pos_i2s_suspend()
  - stops the clock
  - waits for uDMU to finish write outstanding data to buffers
  - clears uDMA channels

* pi_i2s_read()
  - blocking read; blocks until a buffer of data becomes available in L2; returns pointer to the data and its size
  - implemented with read_async(), waiting for task, read_status() to copy from task context
    to the caller's buffer

* pi_i2s_read_async()
  - toggles current_read_buffer
  - if there is a ready buffer, notifies waiting task; otherwise adds task to wait queue

* pos_i2s_enqueue()
  - enqueue uDMA transfer to currently selected buffer
  - if previous transfer is not yet completed, enqueue only remaining size (pending_size)
  - if previous transfer is completed, toggles current_buffer

* pos_i2s_handle_copy()
  - triggered by uDMA when a transfer is completed
  - immediately enqueues next transer (so that there is always +1 transfer pending)
  - if there is a waiter, wakes it up
  - if there is no waiter at the moment, 
 
 ## v : GAP8 I2S driver - PCM from microphone use case
 
  - allocate 2 256 x uint16_t buffers as globals (L2)
  - use pi_i2s_conf_init() to initialize configuration structure
  - modify configuration for this use case: PCM, clk 44100, enable ping-pong, word size 16 bit, 1 channel
  - call pi_i2s_open() which will finalize config (DMA, modes, buffers)
  - use pi_i2s_ioctl() to enable clock and start enqueueing transfers from I2S to L2
  - use pi_i2s_read() in a loop to receive data
 