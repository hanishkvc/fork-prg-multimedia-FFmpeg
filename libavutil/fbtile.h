/*
 * CPU based Framebuffer Generic Tile DeTile logic
 * Copyright (c) 2020 C Hanish Menon <HanishKVC>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVUTIL_FBTILE_H
#define AVUTIL_FBTILE_H

#include <stdint.h>
#include "libavutil/pixfmt.h"
#include "libavutil/frame.h"

/**
 * @file
 * @brief CPU based Framebuffer tiler detiler
 * @author C Hanish Menon <HanishKVC>
 * @{
 */


/**
 * Enable printing of the tile walk
 */
//#define DEBUG_FBTILE 1


/**
 * The FBTile related operations
 */
enum FFFBTileOps {
    FF_FBTILE_OPS_NONE,
    FF_FBTILE_OPS_TILE,
    FF_FBTILE_OPS_DETILE,
    FF_FBTILE_OPS_UNKNOWN,
};

/**
 * The FBTile layout families
 * Used to help map from an external subsystem like say drm
 * to fbtile's internal tile layout id.
 */
enum FFFBTileFamily {
    FF_FBTILE_FAMILY_DRM,
    FF_FBTILE_FAMILY_UNKNOWN,
};

/**
 * The FBTile related Layouts
 * This identifies the supported tile layouts
 */
enum FFFBTileLayout {
    FF_FBTILE_NONE,            // This also corresponds to linear layout
    FF_FBTILE_INTEL_XGEN9,
    FF_FBTILE_INTEL_YGEN9,
    FF_FBTILE_INTEL_YF,
    FF_FBTILE_UNKNOWN,
};

/**
 * FBTile FrameCopy additional status
 */
enum FFFBTileFrameCopyStatus {
    FF_FBTILE_FRAMECOPY_TILECOPY,
    FF_FBTILE_FRAMECOPY_COPYONLY
};


/**
 * Identify equivalent fbtile tile layout id given an external subsystem's tile layout id.
 *
 * @param family identifies the subsystem
 * @param familyTileType the tile layout id as defined by the subsystem
 *
 * @return the fbtile's equivalent tile layout id
 */
enum FFFBTileLayout ff_fbtile_getlayoutid(enum FFFBTileFamily family, uint64_t familyTileType);


/**
 * Supported pixel formats by the fbtile logics
 */
extern const enum AVPixelFormat fbtilePixFormats[];

/**
 * Check if the given pixel formats are supported by fbtile logic.
 *
 * @param srcPixFormat pixel format of source image
 * @param dstPixFormat pixel format of destination image
 *
 * @return 0 if supported, 1 if not
 */
int ff_fbtile_checkpixformats(const enum AVPixelFormat srcPixFormat, const enum AVPixelFormat dstPixFormat);


/**
 * Copy one AVFrame into another, in the process tiling or detiling as required, if possible.
 * NOTE: Either the Source or the Destination AVFrame (i.e one of them) should be linear.
 * NOTE: If the tiling layout is not understood, it falls back to av_frame_copy.
 *
 * @param dst the destination avframe
 * @param dstTileLayout the framebuffer tiling layout expected for the destination avframe
 * @param src the source avframe
 * @param srcTileLayout the framebuffer tiling layout of the source avframe
 * @param status helps identify if only copy was done or (de)tile+copy was done
 *
 * @return 0 if copied.
 */
int ff_fbtile_frame_copy(AVFrame *dst, enum FFFBTileLayout dstTileLayout,
                         AVFrame *src, enum FFFBTileLayout srcTileLayout,
                         enum FFFBTileFrameCopyStatus *status);


/**
 * @}
 */

#endif /* AVUTIL_FBTILE_H */
// vim: set expandtab sts=4: //
