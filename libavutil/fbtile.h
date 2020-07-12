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
 * Set scope of this api to be either public or internal (non-public)
 */
#define FBTILE_SCOPE_PUBLIC 1
#ifdef FBTILE_SCOPE_PUBLIC
#define SCOPEIN
#else
#define SCOPEIN static
#endif

/**
 * Enable printing of the tile walk
 */
//#define DEBUG_FBTILE 1

// Common return values
#define FBT_OK 0
#define FBT_ERR 1

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
 * TileWalk Direction Change Entry
 * Used to specify the tile walking of subtiles within a tile.
 */
struct FFFBTWDirChange {
    int posOffset;
    int xDelta;
    int yDelta;
};


/**
 * TileWalk, Contains info required for a given tile walking.
 *
 * @field bytesPerPixel the bytes per pixel for the image
 * @field subTileWidth the width of subtile within the tile, in pixels
 * @field subTileHeight the height of subtile within the tile, in pixels
 * @field tileWidth the width of the tile, in pixels
 * @field tileHeight the height of the tile, in pixels
 * @field numDirChanges the number of dir changes involved in tile walk
 * @field dirChanges the array of dir changes for the tile walk required
 */
struct FFFBTileWalk {
    int bytesPerPixel;
    int subTileWidth, subTileHeight;
    int tileWidth, tileHeight;
    int numDirChanges;
    struct FFFBTWDirChange dirChanges[];
};


/**
 * FBTile FrameCopy additional status
 */
enum FFFBTileFrameCopyStatus {
    FF_FBTILE_FRAMECOPY_TILECOPY,
    FF_FBTILE_FRAMECOPY_COPYONLY
};


#ifdef FBTILE_SCOPE_PUBLIC


/**
 * Map from formatmodifier to fbtile's internal mode.
 *
 * @param formatModifier the format_modifier to map
 * @return the fbtile's equivalent internal mode
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
int fbtile_checkpixformats(const enum AVPixelFormat srcPixFormat, const enum AVPixelFormat dstPixFormat);


/**
 * Generic Logic.
 */


/**
 * Generic Logic to Tile/Detile between tiled and linear layout.
 *
 * @param op whether to tile or detile
 * @param w width of the image
 * @param h height of the image
 * @param dst the destination image buffer
 * @param dstLineSize the size of each row in dst image, in bytes
 * @param src the source image buffer
 * @param srcLineSize the size of each row in src image, in bytes
 * @param tw the structure which contains the tile walk parameters
 *
 * @return 0 if detiled, 1 if not
 */


/**
 * Generic tile/detile simple version.
 */
int ff_fbtile_generic_simple(enum FFFBTileOps op,
                             const int w, const int h,
                             uint8_t *dst, const int dstLineSize,
                             uint8_t *src, const int srcLineSize,
                             const struct FFFBTileWalk *tw);


/**
 * Generic tile/detile minimal optimised version.
 */
int ff_fbtile_generic_opti(enum FFFBTileOps op,
                           const int w, const int h,
                           uint8_t *dst, const int dstLineSize,
                           uint8_t *src, const int srcLineSize,
                           const struct FFFBTileWalk *tw);


/**
 * Copy one AVFrame into the other, tiling or detiling as required, if possible.
 * NOTE: Either the Source or the Destination AVFrame (i.e one of them) should be linear.
 * NOTE: If the tiling layout is not understood, it will do a simple copy.
 *
 * @param dst the destination avframe
 * @param dstTileLayout the framebuffer tiling layout expected for the destination avframe
 * @param src the source avframe
 * @param srcTileLayout the framebuffer tiling layout of the source avframe
 *
 * @return 0 if copied.
 */
int ff_fbtile_frame_copy(AVFrame *dst, enum FFFBTileLayout dstTileLayout,
                         AVFrame *src, enum FFFBTileLayout srcTileLayout,
                         enum FFFBTileFrameCopyStatus *status);


#endif // FBTILE_SCOPE_PUBLIC


/**
 * @}
 */

#endif /* AVUTIL_FBTILE_H */
// vim: set expandtab sts=4: //
