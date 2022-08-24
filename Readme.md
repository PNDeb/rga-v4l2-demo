# Using the RGA2e with the Pine64 Pinenote

This fork of the rga demo code from https://github.com/wzyy2/rga-v4l2-demo
contains some test programs for the rockchip-rga usage on the Pine64 Pinenote
device.
This device has an epd display which can display 16-level grayscales, requiring
conversion from RGB888 framebuffers to Y4, potentially with dithering-down
support.

Preliminary support for Y4-conversion and dithering-down was added to the
rockchip-rga v4l2 mem2mem driver in this repository:

https://github.com/m-weigand/linux/tree/mw/rk35/rk356x-rga

## Where to start

The program located in the subdirectory **pn_03_drm_blitting_and_ctrls** will
display an image on the pinenote epd display.

RGB->Y4 conversion is done using the RGA hardware.

The RGA hardware can be controlled using key inputs, see the text printed on
execution.
