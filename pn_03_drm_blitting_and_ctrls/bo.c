/*
 * Copyright 2016 Rockchip Electronics S.LSI Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "bo.h"
#include "dev.h"

void fill_bo(struct sp_bo* bo, uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    // draw_rect(bo, 0, 0, bo->width, bo->height, a, r, g, b);
    if (bo->format == DRM_FORMAT_R4) {
		printf("fill_bo: Filling the complete R4 buffer\n");
		memset(bo->map_addr, 0x3, 1872 * 1404 / 2);

		// memset(bo->map_addr, 0x0, 1872 * 700);
		return;
	}
}

void draw_rect(struct sp_bo* bo, uint32_t x, uint32_t y, uint32_t width,
    uint32_t height, uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t i, j, xmax = x + width, ymax = y + height;
    if (bo->format == DRM_FORMAT_R4) {
		printf("Filling the complete R4 buffer\n");
		memset(bo->map_addr, 0xf, 1872 * 1404 / 2);

		memset(bo->map_addr, 0x0, 1872 * 700);
		return;
	}

    if (xmax > bo->width)
        xmax = bo->width;
    if (ymax > bo->height)
        ymax = bo->height;

	// rows
    for (i = y; i < ymax; i++) {
        uint8_t* row = (uint8_t*)bo->map_addr + i * bo->pitch;

		// cols
        for (j = x; j < xmax; j++) {
            uint8_t* pixel = row + j * 4;

            if (bo->format == DRM_FORMAT_ARGB8888 || bo->format == DRM_FORMAT_XRGB8888) {
                pixel[0] = b;
                pixel[1] = g;
                pixel[2] = r;
                pixel[3] = a;
            } else if (bo->format == DRM_FORMAT_RGBA8888) {
                pixel[0] = r;
                pixel[1] = g;
                pixel[2] = b;
                pixel[3] = a;
            } else if (bo->format == DRM_FORMAT_R4) {

            	uint8_t* pixel = row + j * 1;
            }
        }
    }
}

int add_fb_sp_bo(struct sp_bo* bo, uint32_t format)
{
    int ret;
    uint32_t handles[4], pitches[4], offsets[4];

    handles[0] = bo->handle;
    pitches[0] = bo->pitch;
    offsets[0] = 0;

    /* if(format == DRM_FORMAT_NV12 || format == DRM_FORMAT_NV16) { */
    /*     handles[1] = bo->handle; */
    /*     pitches[0] = bo->width; */
    /*     pitches[1] = bo->width; */
    /*     offsets[1] = bo->width * bo->height; */
    /* } */

	printf("add_fb_sp_bo: %lu/%lu %lu\n",
			bo->width, bo->height, format
		);
	if (format == DRM_FORMAT_RGB888){
		printf("    format is DRM_FORMAT_RGB888\n");
	}
	if (format == DRM_FORMAT_R4){
		printf("    format is DRM_FORMAT_R4\n");
	}

    ret = drmModeAddFB2(
		bo->dev->fd,
	   	bo->width,
	   	bo->height,
        format,
	   	handles,
	   	pitches,
	   	offsets,
        &bo->fb_id,
	   	bo->flags
	);
    if (ret) {
        printf("failed to create fb ret=%d\n", ret);
        return ret;
    }
	printf("Got Framebuffer id: %u\n", bo->fb_id);
    return 0;
}

static int map_sp_bo(struct sp_bo* bo)
{
    int ret;
    struct drm_mode_map_dumb md;
	printf("%s, mapping size: %lu \n", __func__, bo->size);

    if (bo->map_addr){
		printf("No mapping because map addr is zero!\n");
        return 0;
	}

    md.handle = bo->handle;
    ret = drmIoctl(bo->dev->fd, DRM_IOCTL_MODE_MAP_DUMB, &md);
    if (ret) {
        printf("failed to map sp_bo ret=%d\n", ret);
        return ret;
    }

    bo->map_addr = mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
        bo->dev->fd, md.offset);
    if (bo->map_addr == MAP_FAILED) {
        printf("failed to map bo ret=%d\n", -errno);
        return -errno;
    }
	memset(bo->map_addr, 0xf, bo->size);
    return 0;
}

struct sp_bo* create_sp_bo(struct sp_dev* dev, uint32_t width, uint32_t height,
    uint32_t depth, uint32_t bpp, uint32_t format, uint32_t flags)
{
    int ret;
    struct drm_mode_create_dumb cd;
    struct sp_bo* bo;

    memset(&cd, 0, sizeof(cd));

    bo = (sp_bo *) calloc(1, sizeof(*bo));
    if (!bo)
        return NULL;

    cd.height = height;
    cd.width = width;
	if (bpp == 4){
		cd.bpp = 4;
	}
	else {
		cd.bpp = bpp;
	}
	// cd.bpp = 24;
    cd.flags = flags;
	printf("We are requesting a DUMB buffer for width/height: %lu/%lu bpp: %lu flags: %lu\n",
			width,
			height,
			bpp,
			flags
		);

    ret = drmIoctl(dev->fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd);
    if (ret) {
        printf("failed to create sp_bo %d\n", ret);
        goto err;
    }

    bo->dev = dev;
    bo->width = width;
    bo->height = height;
    bo->depth = depth;
    // bo->depth = 24;
    bo->bpp = bpp;
    bo->format = format;
    bo->flags = flags;

    bo->handle = cd.handle;
    bo->pitch = cd.pitch;
	/* if (bpp == 4){ */
	/* 	bo->size = 1872 * 1404 / 2; */
	/* } else { */
    	/* bo->size = cd.size; */
	/* } */
	bo->size = cd.size;
	printf("Got Framebuffer of size: %lu (real: %lu)\n", bo->size, cd.size);

	// we only want to actually scan out the R4 buffer
	if (format == DRM_FORMAT_R4){
		printf("Adding R4 Framebuffer\n");
		ret = add_fb_sp_bo(bo, format);
		if (ret) {
			printf("failed to add fb ret=%d\n", ret);
			goto err;
		}
	}

    ret = map_sp_bo(bo);
    if (ret) {
        printf("failed to map bo ret=%d\n", ret);
        goto err;
    }

    return bo;

err:
    free_sp_bo(bo);
    return NULL;
}

void free_sp_bo(struct sp_bo* bo)
{
    int ret;
    struct drm_mode_destroy_dumb dd;

    if (!bo)
        return;

    if (bo->map_addr)
        munmap(bo->map_addr, bo->size);

    if (bo->fb_id) {
        ret = drmModeRmFB(bo->dev->fd, bo->fb_id);
        if (ret)
            printf("Failed to rmfb ret=%d!\n", ret);
    }

    if (bo->handle) {
        dd.handle = bo->handle;
        ret = drmIoctl(bo->dev->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        if (ret)
            printf("Failed to destroy buffer ret=%d\n", ret);
    }

    free(bo);
}