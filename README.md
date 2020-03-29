# 2020_gap8
GAP8 test project

## i : Helloworld

Helloworld written by looking at provided example for PULP-OS. Some comments added to try to show understanding of the code.

## iv : 

Assumptions about the problem:
- VGA grayscale 640x480, 8bpp, @30Hz => 300kB/30Hz

## v : GAP8 I2S driver

The driver receives input PCM stream by using uDMA and stores it in buffers provided by user.
Minimum of 2 buffers are used, so that receiving can continue while user is reading previous buffer from his side. 2 buffer modes are supported: ping-pong and ring buffer of arbitrary size.
A blocking read is provided to the user. PMSIS task support is used to queue and wake up waiters.
Brief description of driver functions is given below.

- pi_i2s_conf_init()
  Initialize interface configuration to default values

- pi_i2s_open()
  - apply user conf to driver:
    - configure DMA channel and callback event
    - configure data format (PDM vs PCM)
    - for slab mode, allocate ring buffer in FC L1
    - for PDM mode, configure PDM shift
    - for PCM mode, configure bit frequency

- pi_i2s_ioctl()
  - start/stop (resume/suspend)

- pos_i2s_resume()
  - enqueue 2 buffers to be used by uDMA to do transfer from I2S to L2
  - start I2S clock

- pos_i2s_suspend()
  - stops the clock
  - waits for uDMU to finish write outstanding data to buffers
  - clears uDMA channels

- pi_i2s_read()
  - blocking read of 'size' data; blocks until 'size' block is received
  - implemented with read_async(), waiting for task, read_status() to copy from task context
    to the caller's buffer

- pi_i2s_read_async()
  - toggles current_read_buffer
  - if there is a ready buffer, notifies waiting task; otherwise adds task to wait queue

- pos_i2s_enqueue()
  - enqueue uDMA transfer to currently selected buffer
  - if previous transfer is not yet completed, enqueue only remaining size (pending_size)
  - if previous transfer is completed, toggles current_buffer

- pos_i2s_handle_copy()
  - triggered by uDMA when a transfer is completed
  - immediately enqueues next transer (so that there is always +1 transfer pending)
  - if there is a waiter, wakes it up
  - if there is no waiter at the moment, 
  