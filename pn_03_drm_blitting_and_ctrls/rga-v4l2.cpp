/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Jacob Chen <jacob-chen@iotwrt.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version
 */
/* Modified by Maximilian Weigand (mweigand@mweigand.net), 2022 */

#include <asm/types.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <linux/stddef.h>
#include <linux/videodev2.h>

#include "bo.h"
#include "dev.h"

#include "modeset.h"

/* operation values */
#define V4L2_CID_BLEND			(V4L2_CID_IMAGE_PROC_CLASS_BASE + 4)

enum v4l2_blend_mode {
	V4L2_BLEND_SRC			= 0,
	V4L2_BLEND_SRCATOP		= 1,
	V4L2_BLEND_SRCIN		= 2,
	V4L2_BLEND_SRCOUT		= 3,
	V4L2_BLEND_SRCOVER		= 4,
	V4L2_BLEND_DST			= 5,
	V4L2_BLEND_DSTATOP		= 6,
	V4L2_BLEND_DSTIN		= 7,
	V4L2_BLEND_DSTOUT		= 8,
	V4L2_BLEND_DSTOVER		= 9,
	V4L2_BLEND_ADD			= 10,
	V4L2_BLEND_CLEAR		= 11,
};

// store settings for our custom ioctls
struct ioctl_values {
	uint32_t dither_down_enable;
	uint32_t dither_down_mode;
	uint32_t lut0;
	uint32_t lut1;

};

struct ioctl_values ioctl_control;

#define NUM_BUFS 1

static char* mem2mem_dev_name = NULL;

static int hflip = 0;
static int vflip = 0;
static int rotate = 0;
static int fill_color = 0;
static int op = 0;
static int num_frames = 1;
static int display = 1;

static size_t SRC_WIDTH = 1872;
static size_t SRC_HEIGHT = 1404;

static size_t SRC_CROP_X = 0;
static size_t SRC_CROP_Y = 0;
static size_t SRC_CROP_W = 0;
static size_t SRC_CROP_H = 0;

static size_t DST_WIDTH = 1872;
static size_t DST_HEIGHT = 1404;

static size_t DST_CROP_X = 0;
static size_t DST_CROP_Y = 0;
static size_t DST_CROP_W = 0;
static size_t DST_CROP_H = 0;

static int src_format = V4L2_PIX_FMT_RGB24;
// static int dst_format = V4L2_PIX_FMT_XRGB32;
static int dst_format = V4L2_PIX_FMT_Y4;

static struct timespec start, end;
static unsigned long long time_consumed;
static int mem2mem_fd;

static void *p_src_buf[NUM_BUFS], *p_dst_buf[NUM_BUFS];
static int src_buf_fd[NUM_BUFS], dst_buf_fd[NUM_BUFS];
static struct sp_bo *src_buf_bo[NUM_BUFS], *dst_buf_bo[NUM_BUFS];
static size_t src_buf_size[NUM_BUFS], dst_buf_size[NUM_BUFS];
static unsigned int num_src_bufs = 0, num_dst_bufs = 0;

static struct sp_dev* dev_sp;
static struct sp_plane** plane_sp;
static struct sp_crtc* test_crtc_sp;
static struct sp_plane* test_plane_sp;

static unsigned int get_drm_format(unsigned int v4l2_format)
{
    switch (v4l2_format) {
    case V4L2_PIX_FMT_NV12: //0
        return DRM_FORMAT_NV12;
    case V4L2_PIX_FMT_ARGB32: //1
        return DRM_FORMAT_ARGB8888;
    case V4L2_PIX_FMT_RGB24: //2
        return DRM_FORMAT_RGB888;
    case V4L2_PIX_FMT_RGB565: //3
        return DRM_FORMAT_RGB565;
    case V4L2_PIX_FMT_YUV420: //4
        return DRM_FORMAT_YUV420;
    case V4L2_PIX_FMT_XRGB32: //5
        return DRM_FORMAT_XRGB8888;
    case V4L2_PIX_FMT_ABGR32: //6
        return DRM_FORMAT_BGRA8888;
    case V4L2_PIX_FMT_XBGR32: //7
        return DRM_FORMAT_BGRX8888;
    case V4L2_PIX_FMT_ARGB555: //8
        return DRM_FORMAT_ARGB1555;
    case V4L2_PIX_FMT_ARGB444: //9
        return DRM_FORMAT_ARGB4444;
    case V4L2_PIX_FMT_NV61: // 10
        return DRM_FORMAT_NV61;
    case V4L2_PIX_FMT_NV16: //11
        return DRM_FORMAT_NV16;
    case V4L2_PIX_FMT_YUV422P: //12
        return DRM_FORMAT_YUV422;
    case V4L2_PIX_FMT_Y4: // 13
        return DRM_FORMAT_R4;
    }
    return DRM_FORMAT_NV12;
}
void fillbuffer(unsigned int v4l2_format, struct sp_bo* bo, unsigned int frame_counter)
{
    if (v4l2_format == V4L2_PIX_FMT_RGB24) {
        uint8_t* buf = (uint8_t*)bo->map_addr;
        int i, j;

		// fill pattern
		if (0){
			unsigned int loc;
			loc = frame_counter % (1872 - 150);
			printf("Filling, loc: %i\n", loc);

			for (j = 0; j < bo->height; j += 1) {
				for (i = 0; i < bo->width; i += 1) {

					uint8_t color;

					if ((i >= loc) && (i < (loc + 150)))
						color = 0x33;
					else
						// background
						color = 0xff;

					*(buf++) = color;
					*(buf++) = color;
					*(buf++) = color;
				}
			}
		}
		if(1){
			FILE * src_file;
			src_file = fopen("spheres_rgb.bin", "rb");
			fread(buf, sizeof(uint8_t), 1872 * 1404 * 3, src_file);
			fclose(src_file);
		}
	} else {
		printf("no filling for this format\n");
	}
}

void fillbuffer2(unsigned int v4l2_format, struct sp_bo* bo)
{
    if (v4l2_format == V4L2_PIX_FMT_ARGB32) {
        uint32_t* buf = (uint32_t*)bo->map_addr;
        int i, j;

        for (j = 0; j < bo->height; j += 1) {

            for (i = 0; i < bo->width / 2; i += 1) {
                *(buf++) = 0x550000ff;
            }
        }
	} else {
		printf("no filling for this format\n");
	}
}

int get_v4l2_control_old(const char *name){
	struct v4l2_queryctrl queryctrl;
	int control_id = -1;
	memset(&queryctrl, 0, sizeof(queryctrl));

	queryctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
	while (0 == ioctl(mem2mem_fd, VIDIOC_QUERYCTRL, &queryctrl)) {
		if (!(queryctrl.flags & V4L2_CTRL_FLAG_DISABLED)) {
			/* printf("Control id: %x %s\n", queryctrl.id, queryctrl.name); */
			if (!strcmp((char*)queryctrl.name, name)){
				control_id = queryctrl.id;
				/* printf("Found it\n"); */
			}
		}

		queryctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
	}
	return control_id;
}


static void init_mem2mem_dev()
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_control ctrl;
    struct v4l2_crop crop;
    int ret;

    mem2mem_fd = open(mem2mem_dev_name, O_RDWR | O_CLOEXEC, 0);
    if (mem2mem_fd < 0) {
        fprintf(stderr, "%s:%d: ", __func__, __LINE__);
        perror("open");
        return;
    }

    if (hflip != 0) {
        ctrl.id = V4L2_CID_HFLIP;
        ctrl.value = 1;
        ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);
        if (ret != 0)
            fprintf(stderr, "%s:%d: Set HFLIP failed\n",
                __func__, __LINE__);
    }

    if (vflip != 0) {
        ctrl.id = V4L2_CID_VFLIP;
        ctrl.value = 1;
        ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);
        if (ret != 0)
            fprintf(stderr, "%s:%d: Set VFLIP failed\n",
                __func__, __LINE__);
    }

    if (rotate != 0) {
        ctrl.id = V4L2_CID_ROTATE;
        ctrl.value = rotate;
        ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);
        if (ret != 0)
            fprintf(stderr, "%s:%d: Set ROTATE failed\n",
                __func__, __LINE__);
    }

    if (fill_color != 0) {
        ctrl.id = V4L2_CID_BG_COLOR;
        ctrl.value = fill_color;
        ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);
        if (ret != 0)
            fprintf(stderr, "%s:%d: Set Fill Color failed\n",
                __func__, __LINE__);
    }

	ctrl.id = get_v4l2_control_old("Enable Y4 conversion");
	ctrl.value = 1;
	ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);
	if (ret != 0)
		fprintf(stderr, "%s:%d: Set Fill Color failed\n",
			__func__, __LINE__);

	ctrl.id = get_v4l2_control_old("Enable Y400 conversion");
	ctrl.value = 1;
	ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);
	if (ret != 0)
		fprintf(stderr, "%s:%d: Set Fill Color failed\n",
			__func__, __LINE__);

#if 0
    ctrl.id = V4L2_CID_BLEND;
    ctrl.value = op;
    ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);
    if (ret != 0)
        fprintf(stderr, "%s:%d: Set OP failed\n",
            __func__, __LINE__);
#endif
    ret = ioctl(mem2mem_fd, VIDIOC_QUERYCAP, &cap);
    if (ret != 0) {
        fprintf(stderr, "%s:%d: ", __func__, __LINE__);
        perror("ioctl");
        return;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_M2M)) {
        fprintf(stderr, "Device does not support m2m\n");
        exit(EXIT_FAILURE);
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Device does not support streaming\n");
        exit(EXIT_FAILURE);
    }

    /* Set format for output */
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = SRC_WIDTH;
    fmt.fmt.pix.height = SRC_HEIGHT;
    fmt.fmt.pix.pixelformat = src_format;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

	printf("stride 1: %i\n", fmt.fmt.pix.bytesperline);
    ret = ioctl(mem2mem_fd, VIDIOC_S_FMT, &fmt);
    if (ret != 0) {
        fprintf(stderr, "%s:%d: ", __func__, __LINE__);
        perror("ioctl");
        return;
    }
	printf("stride 2: %i\n", fmt.fmt.pix.bytesperline);

    /* Set format for capture */
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = DST_WIDTH;
    fmt.fmt.pix.height = DST_HEIGHT;
    fmt.fmt.pix.pixelformat = dst_format;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    ret = ioctl(mem2mem_fd, VIDIOC_S_FMT, &fmt);
    if (ret != 0) {
        fprintf(stderr, "%s:%d: ", __func__, __LINE__);
        perror("ioctl");
        return;
    }
	printf("stride 3: %i\n", fmt.fmt.pix.bytesperline);

    printf("crop was replaced by selection \n");
}

static void process_mem2mem_frame()
{
    struct v4l2_buffer buf;
    int ret, i;
	int frame_counter = 0;
	unsigned int modifier_key;
	printf("process_mem2mem_frame\n");

    i = num_frames;
    //while (i--) {
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &start);

        memset(&(buf), 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.bytesused = src_buf_size[0];
        buf.index = 0;
        buf.m.fd = src_buf_fd[0];
        ret = ioctl(mem2mem_fd, VIDIOC_QBUF, &buf);
        if (ret != 0) {
            fprintf(stderr, "%s:%d: ", __func__, __LINE__);
            perror("ioctl");
            return;
        }

        memset(&(buf), 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.index = 0;
        buf.m.fd = dst_buf_fd[0];
        ret = ioctl(mem2mem_fd, VIDIOC_QBUF, &buf);
        if (ret != 0) {
            fprintf(stderr, "%s:%d: ", __func__, __LINE__);
            perror("ioctl");
            return;
        }

        memset(&(buf), 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf.memory = V4L2_MEMORY_DMABUF;
        ret = ioctl(mem2mem_fd, VIDIOC_DQBUF, &buf);
        printf("Dequeued source buffer, index: %d\n", buf.index);

        memset(&(buf), 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_DMABUF;
        ret = ioctl(mem2mem_fd, VIDIOC_DQBUF, &buf);
        if (ret != 0) {
            fprintf(stderr, "%s:%d: ", __func__, __LINE__);
            perror("ioctl");
            return;
        }
        printf("Dequeued dst buffer, index: %d\n", buf.index);

        clock_gettime(CLOCK_MONOTONIC, &end);

        time_consumed = (end.tv_sec - start.tv_sec) * 1000000000ULL;
        time_consumed += (end.tv_nsec - start.tv_nsec);
        time_consumed /= 1000;

        printf("*[RGA]* : used %f msecs\n", time_consumed * 1.0 / 1000);

        if (display == 1) {
            test_plane_sp->bo = dst_buf_bo[buf.index];
            set_sp_plane(dev_sp, test_plane_sp, test_crtc_sp, 0, 0);

            if (0) {
                unsigned int *addr = (unsigned int *)test_plane_sp->bo->map_addr;
                printf("dump buffer : %x %x %x \n", addr[0], addr[1], addr[2]);
                usleep(1000 * 1000);
                printf("dump buffer2 : %x %x %x \n", addr[0], addr[1], addr[2]);
            }
        }
		// modify the buffer
        fillbuffer(src_format, src_buf_bo[0], frame_counter);

		frame_counter++;
		printf("Press a control key\n");
		printf("r: rotate by 90 degrees and back\n");
		printf("v: enable/disable VFLIP\n");
		printf("h: enable/disable HFLIP\n");
		printf("d: enable/disable dither down (make sure to change dither mode with m to see visual changes\n");
		printf("m: cycle through dither modes\n");
		printf("l: modify the y4map lut0/1 (defunct)\n");
		printf("\n");
		printf("Input: ");
		modifier_key = getchar();
		while (	modifier_key == '\n')
			modifier_key = getchar();
		struct v4l2_control ctrl;
		switch(modifier_key){
			case 'r':
				printf("Rotating\n");
				if (rotate == 0)
					rotate = 90;
				else
					rotate = 0;
				ctrl.id = V4L2_CID_ROTATE;
				ctrl.value = rotate;
    			ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);
				if (!ret)
					printf("ioctl succeeded\n");
				else
					printf("ioctl error: %i\n", ret);
				break;
			case 'v':
				printf("V4L2_CID_VFLIP\n");
				vflip = !vflip;
				ctrl.id = V4L2_CID_VFLIP;
				ctrl.value = vflip;
    			ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);
				if (!ret)
					printf("ioctl succeeded\n");
				else
					printf("ioctl error: %i\n", ret);
				break;
			case 'h':
				printf("V4L2_CID_HFLIP\n");
				hflip = !hflip;
				ctrl.id = V4L2_CID_HFLIP;
				ctrl.value = hflip;
    			ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);
				if (!ret)
					printf("ioctl succeeded\n");
				else
					printf("ioctl error: %i\n", ret);
				break;
			case 'd':
				printf("Dither down (old value: %u)\n", ioctl_control.dither_down_enable);
				ioctl_control.dither_down_enable = !ioctl_control.dither_down_enable;
				printf("            (new value: %u)\n", ioctl_control.dither_down_enable);
				ctrl.id = get_v4l2_control_old("Enable dither down");
				ctrl.value = ioctl_control.dither_down_enable;
    			ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);
				if (!ret)
					printf("ioctl succeeded\n");
				else
					printf("ioctl error: %i\n", ret);
				break;
			case 'm':
				// first disable dithering
				ctrl.id = get_v4l2_control_old("Enable dither down");
				ctrl.value = 0;
    			ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);

				printf("Dither down mode (old value: %u)\n", ioctl_control.dither_down_mode);
				ioctl_control.dither_down_mode = (ioctl_control.dither_down_mode + 1) % 4;
				printf("            (new value: %u)\n", ioctl_control.dither_down_mode);
				ctrl.id = get_v4l2_control_old("Dither down mode");
				ctrl.value = ioctl_control.dither_down_mode;
    			ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);
				if (!ret)
					printf("ioctl succeeded\n");
				else
					printf("ioctl error: %i\n", ret);

				// potentially reenable dithering
				ctrl.id = get_v4l2_control_old("Enable dither down");
				ctrl.value = ioctl_control.dither_down_enable;
    			ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);
				break;
			case 'l':
				printf("Change lut0/1\n");
				/* ctrl.id = get_v4l2_control_old("Set Y4MAP LUT0"); */
				/* ctrl.value = ioctl_control.lut0 = 0x89abcdef; */
    			/* ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl); */
				/* if (!ret) */
				/* 	printf("ioctl succeeded\n"); */
				/* else */
				/* 	printf("ioctl error: %i\n", ret); */

				// lut1
				ctrl.id = get_v4l2_control_old("Set Y4MAP LUT1");
				ctrl.value = ioctl_control.lut0 = 0x1234567;
    			ret = ioctl(mem2mem_fd, VIDIOC_S_CTRL, &ctrl);
				if (!ret)
					printf("ioctl succeeded\n");
				else
					printf("ioctl error: %i\n", ret);
				break;

		}
		modifier_key = 0;
    }

    printf("press <ENTER> to exit test application\n");
    getchar();
}


static void start_mem2mem()
{
    int ret, i;
    struct v4l2_buffer buf;
    struct v4l2_requestbuffers reqbuf;
    enum v4l2_buf_type type;

    init_mem2mem_dev();

    memset(&(buf), 0, sizeof(buf));
    reqbuf.count = 1;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    reqbuf.memory = V4L2_MEMORY_DMABUF;
    ret = ioctl(mem2mem_fd, VIDIOC_REQBUFS, &reqbuf);
    if (ret != 0) {
        fprintf(stderr, "%s:%d: ", __func__, __LINE__);
        perror("ioctl");
        return;
    }
    num_src_bufs = reqbuf.count;
    printf("Got %d src buffers\n", num_src_bufs);

    reqbuf.count = NUM_BUFS;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(mem2mem_fd, VIDIOC_REQBUFS, &reqbuf);
    if (ret != 0) {
        fprintf(stderr, "%s:%d: ", __func__, __LINE__);
        perror("ioctl");
        return;
    }
    num_dst_bufs = reqbuf.count;
    printf("Got %d dst buffers\n", num_dst_bufs);

    for (i = 0; i < num_src_bufs; ++i) {
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.index = i;
        ret = ioctl(mem2mem_fd, VIDIOC_QUERYBUF, &buf);
        if (ret != 0) {
            fprintf(stderr, "%s:%d: ", __func__, __LINE__);
            perror("ioctl");
            return;
        }

        src_buf_size[i] = buf.length;

        struct sp_bo* bo
            = create_sp_bo(dev_sp, SRC_WIDTH, SRC_HEIGHT, 0, buf.length * 8 / (SRC_WIDTH * SRC_HEIGHT), get_drm_format(src_format), 0);
        if (!bo) {
            printf("Failed to create gem buf\n");
            exit(-1);
        }

        drmPrimeHandleToFD(dev_sp->fd, bo->handle, 0, &src_buf_fd[i]);
        src_buf_bo[i] = bo;
        fillbuffer(src_format, src_buf_bo[i], 0);
    }

    for (i = 0; i < num_dst_bufs; ++i) {
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.index = i;
        ret = ioctl(mem2mem_fd, VIDIOC_QUERYBUF, &buf);
        if (ret != 0) {
            fprintf(stderr, "%s:%d: ", __func__, __LINE__);
            perror("ioctl");
            return;
        }
		printf("DST BUFFER LENGTH: %u\n", buf.length);

        dst_buf_size[i] = buf.length;
        struct sp_bo* bo
            = create_sp_bo(dev_sp, DST_WIDTH, DST_HEIGHT, 0, buf.length * 8 / (DST_WIDTH * DST_HEIGHT), get_drm_format(dst_format), 0);
        if (!bo) {
            printf("Failed to create gem buf\n");
            exit(-1);
        }

        drmPrimeHandleToFD(dev_sp->fd, bo->handle, 0, &dst_buf_fd[i]);
        dst_buf_bo[i] = bo;
        fillbuffer2(dst_format, bo);
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(mem2mem_fd, VIDIOC_STREAMON, &type);
    if (ret != 0) {
        fprintf(stderr, "%s:%d: ", __func__, __LINE__);
        perror("ioctl");
        return;
    }

    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = ioctl(mem2mem_fd, VIDIOC_STREAMON, &type);
    if (ret != 0) {
        fprintf(stderr, "%s:%d: ", __func__, __LINE__);
        perror("ioctl");
        return;
    }

    process_mem2mem_frame();

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(mem2mem_fd, VIDIOC_STREAMOFF, &type);
    if (ret != 0) {
        fprintf(stderr, "%s:%d: ", __func__, __LINE__);
        perror("ioctl");
        return;
    }

    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = ioctl(mem2mem_fd, VIDIOC_STREAMOFF, &type);
    if (ret != 0) {
        fprintf(stderr, "%s:%d: ", __func__, __LINE__);
        perror("ioctl");
        return;
    }

    close(mem2mem_fd);
}

void init_drm_context()
{
    int ret, i;
    dev_sp = create_sp_dev();
    if (!dev_sp) {
        printf("create_sp_dev failed\n");
        exit(-1);
    }

    if (display) {
        ret = initialize_screens(dev_sp);
        if (ret) {
            printf("initialize_screens failed\n");
            printf("please close display server for test!\n");
            exit(-1);
        }

        plane_sp = (struct sp_plane**)calloc(dev_sp->num_planes, sizeof(*plane_sp));
        if (!plane_sp) {
            printf("calloc plane array failed\n");
            exit(-1);
            ;
        }

		printf("If nothing is display, change it to crtcs[1] \n");
        test_crtc_sp = &dev_sp->crtcs[0];
        for (i = 0; i < test_crtc_sp->num_planes; i++) {
            plane_sp[i] = get_sp_plane(dev_sp, test_crtc_sp);
            if (is_supported_format(plane_sp[i], get_drm_format(dst_format)))
                test_plane_sp = plane_sp[i];
			else
				printf("NOT is_supported\n");
        }
        if (!test_plane_sp) {
            printf("test_plane is NULL\n");
            exit(-1);
        }
    }
}

static void usage(FILE* fp, int argc, char** argv)
{
    fprintf(fp,
        "Usage: %s [options]\n\n"
        "Options:\n"
        "--device                   mem2mem device name [/dev/video0]\n"
        "--hel                      Print this message\n"
        "--src-fmt                  Source video format, 0 = NV12, 1 = ARGB32, 2 = RGB888\n"
        "--src-width                Source video width\n"
        "--src-height               Source video height\n"
        "--src-crop-x               Source video crop X offset [0]\n"
        "--src-crop-y               Source video crop Y offset [0]\n"
        "--src-crop-width           Source video crop width [width]\n"
        "--src-crop-height          Source video crop height [height]\n"
        "--dst-fmt                  Destination video format, 0 = NV12, 1 = ARGB32, 2 = RGB888\n"
        "--dst-width                Destination video width\n"
        "--dst-height               Destination video height\n"
        "--dst-crop-x               Destination video crop X offset [0]\n"
        "--dst-crop-y               Destination video crop Y offset [0]\n"
        "--dst-crop-width           Destination video crop width [width]\n"
        "--dst-crop-height          Destination video crop height [height]\n"
        "--op                       Transform operations\n"
        "--fill-color               Solid fill color\n"
        "--rotate                   Rotate\n"
        "--hflip                    Horizontal Mirror\n"
        "--vflip                    Vertical Mirror\n"
        "--num-frames               Number of frames to process [100]\n"
        "--display                  Display\n"
        "",
        argv[0]);
}

static const struct option long_options[] = {
    { "device", required_argument, NULL, 0 },
    { "help", no_argument, NULL, 0 },
    { "src-fmt", required_argument, NULL, 0 },
    { "src-width", required_argument, NULL, 0 },
    { "src-height", required_argument, NULL, 0 },
    { "src-crop-x", required_argument, NULL, 0 },
    { "src-crop-y", required_argument, NULL, 0 },
    { "src-crop-width", required_argument, NULL, 0 },
    { "src-crop-height", required_argument, NULL, 0 },
    { "dst-fmt", required_argument, NULL, 0 },
    { "dst-width", required_argument, NULL, 0 },
    { "dst-height", required_argument, NULL, 0 },
    { "dst-crop-x", required_argument, NULL, 0 },
    { "dst-crop-y", required_argument, NULL, 0 },
    { "dst-crop-width", required_argument, NULL, 0 },
    { "dst-crop-height", required_argument, NULL, 0 },
    { "op", required_argument, NULL, 0 },
    { "fill-color", required_argument, NULL, 0 },
    { "rotate", required_argument, NULL, 0 },
    { "hflip", required_argument, NULL, 0 },
    { "vflip", required_argument, NULL, 0 },
    { "num-frames", required_argument, NULL, 0 },
    { "display", required_argument, NULL, 0 },
    { 0, 0, 0, 0 }
};

int main(int argc, char** argv)
{
    int i;
    mem2mem_dev_name = (char*)"/dev/video0";

    for (;;) {
        int index;
        int c;

        c = getopt_long(argc, argv, "", long_options, &index);

        if (-1 == c)
            break;

        switch (index) {
        case 0: /* getopt_long() flag */
            mem2mem_dev_name = optarg;
            break;

        case 1:
            usage(stdout, argc, argv);
            exit(EXIT_SUCCESS);

        case 2:
            c = atoi(optarg);
            if (c == 0) {
                src_format = V4L2_PIX_FMT_NV12;
            } else if (c == 1) {
                src_format = V4L2_PIX_FMT_ARGB32;
            } else if (c == 2) {
                src_format = V4L2_PIX_FMT_RGB24;
            } else if (c == 3) {
                src_format = V4L2_PIX_FMT_RGB565;
            } else if (c == 4) {
                src_format = V4L2_PIX_FMT_YUV420;
            } else if (c == 5) {
                src_format = V4L2_PIX_FMT_XRGB32;
            } else if (c == 6) {
                src_format = V4L2_PIX_FMT_ABGR32;
            } else if (c == 7) {
                src_format = V4L2_PIX_FMT_XBGR32;
            } else if (c == 8) {
                src_format = V4L2_PIX_FMT_ARGB555;
            } else if (c == 9) {
                src_format = V4L2_PIX_FMT_ARGB444;
            } else if (c == 10) {
                src_format = V4L2_PIX_FMT_NV61;
            } else if (c == 11) {
                src_format = V4L2_PIX_FMT_NV16;
            } else if (c == 12) {
                src_format = V4L2_PIX_FMT_YUV422P;
            } else if (c == 13) {
                src_format = V4L2_PIX_FMT_Y4;
            }
            break;
        case 3:
            SRC_WIDTH = atoi(optarg);
            break;
        case 4:
            SRC_HEIGHT = atoi(optarg);
            break;
        case 5:
            SRC_CROP_X = atoi(optarg);
            break;
        case 6:
            SRC_CROP_Y = atoi(optarg);
            break;
        case 7:
            SRC_CROP_W = atoi(optarg);
            break;
        case 8:
            SRC_CROP_H = atoi(optarg);
            break;
        case 9:
            c = atoi(optarg);
            if (c == 0) {
                dst_format = V4L2_PIX_FMT_NV12;
            } else if (c == 1) {
                dst_format = V4L2_PIX_FMT_ARGB32;
            } else if (c == 2) {
                dst_format = V4L2_PIX_FMT_RGB24;
            } else if (c == 3) {
                dst_format = V4L2_PIX_FMT_RGB565;
            } else if (c == 4) {
                dst_format = V4L2_PIX_FMT_YUV420;
            } else if (c == 5) {
                dst_format = V4L2_PIX_FMT_XRGB32;
            } else if (c == 6) {
                dst_format = V4L2_PIX_FMT_ABGR32;
            } else if (c == 7) {
                dst_format = V4L2_PIX_FMT_XBGR32;
            } else if (c == 8) {
                dst_format = V4L2_PIX_FMT_ARGB555;
            } else if (c == 9) {
                dst_format = V4L2_PIX_FMT_ARGB444;
            } else if (c == 10) {
                dst_format = V4L2_PIX_FMT_NV61;
            } else if (c == 11) {
                dst_format = V4L2_PIX_FMT_NV16;
            } else if (c == 12) {
                dst_format = V4L2_PIX_FMT_YUV422P;
            } else if (c == 13) {
                src_format = V4L2_PIX_FMT_Y4;
            }
            break;
        case 10:
            DST_WIDTH = atoi(optarg);
            break;
        case 11:
            DST_HEIGHT = atoi(optarg);
            break;
        case 12:
            DST_CROP_X = atoi(optarg);
            break;
        case 13:
            DST_CROP_Y = atoi(optarg);
            break;
        case 14:
            DST_CROP_W = atoi(optarg);
            break;
        case 15:
            DST_CROP_H = atoi(optarg);
            break;
        case 16:
            op = atoi(optarg);
            break;
        case 17:
            sscanf(optarg, "%x", &fill_color);
            break;
        case 18:
            rotate = atoi(optarg);
            break;
        case 19:
            hflip = atoi(optarg);
            break;
        case 20:
            vflip = atoi(optarg);
            break;
        case 21:
            num_frames = atoi(optarg);
            break;
        case 22:
            display = atoi(optarg);
            break;
        default:
            usage(stderr, argc, argv);
            exit(EXIT_FAILURE);
        }
    }

	printf("drm\n");
    init_drm_context();

	printf("drm 2\n");
    start_mem2mem();

    for (i = 0; i < num_src_bufs; ++i) {
        close(src_buf_fd[i]);
        free_sp_bo(src_buf_bo[i]);
    }

    for (i = 0; i < num_dst_bufs; ++i) {
        close(dst_buf_fd[i]);
        free_sp_bo(dst_buf_bo[i]);
    }

    if (display)
        test_plane_sp->bo = NULL;
    destroy_sp_dev(dev_sp);

    return 0;
}
