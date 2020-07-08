/*
 * CPU based Framebuffer Tile DeTile logic
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


// Enable printing of the tile walk
//#define DEBUG_FBTILE 1


// Common return values
#define FBT_OK 0
#define FBT_ERR 1


/**
 * The FBTile related modes
 * This identifies the supported tile layouts
 */
enum FBTileMode {
    TILE_NONE,
    TILE_INTELX,
    TILE_INTELY,
    TILE_INTELYF,
    TILE_UNKNOWN,
};


/**
 * Map from formatmodifier to fbtile's internal mode.
 *
 * @param formatModifier the format_modifier to map
 * @return the fbtile's equivalent internal mode
 */
#undef DEBUG_FBTILE_FORMATMODIFIER_MAPPING
enum FBTileMode fbtilemode_from_drmformatmodifier(uint64_t formatModifier);


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

/*
 * Direction Change Entry
 * Used to specify the tile walking of subtiles within a tile.
 */
struct dirChange {
    int posOffset;
    int xDelta;
    int yDelta;
};

/*
 * TileWalk, Contains info required for a given tile walking.
 */
struct TileWalk {
    int bytesPerPixel;
    int subTileWidth, subTileHeight;
    int tileWidth, tileHeight;
    int numDirChanges;
    struct dirChange dirChanges[];
};

/**
 * Tile Walk parameters for Tile-X, Tile-Y, Tile-Yf
 */
extern struct TileWalk tyfTileWalk;
extern struct TileWalk txTileWalk;
extern struct TileWalk tyTileWalk;


/**
 * Generic Logic to Detile into linear layout.
 *
 * @param w width of the image
 * @param h height of the image
 * @param dst the destination image buffer
 * @param dstLineSize the size of each row in dst image, in bytes
 * @param src the source image buffer
 * @param srcLineSize the size of each row in src image, in bytes
 * the wide _func additional explicit options
 *     @param bytesPerPixel the bytes per pixel for the image
 *     @param subTileWidth the width of subtile within the tile, in pixels
 *     @param subTileHeight the height of subtile within the tile, in pixels
 *     @param tileWidth the width of the tile, in pixels
 *     @param tileHeight the height of the tile, in pixels
 *     @param numDirChanges the number of dir changes involved in tile walk
 *     @param dirChanges the array of dir changes for the tile walk required
 * the compact func additional options
 *     @param tw the structure which contains the tile walk parameters
 *
 * @return 0 if detiled, 1 if not
 */


/**
 * Generic tile/detile simple version.
 */
int _fbtiler_generic_simple(const int w, const int h,
                           uint8_t *dst, const int dstLineSize,
                           const uint8_t *src, const int srcLineSize,
                           const int bytesPerPixel,
                           const int subTileWidth, const int subTileHeight,
                           const int tileWidth, const int tileHeight,
                           const int numDirChanges, const struct dirChange *dirChanges,
                           int op);
int fbtiler_generic_simple(const int w, const int h,
                          uint8_t *dst, const int dstLineSize,
                          const uint8_t *src, const int srcLineSize,
                          const struct TileWalk *tw, int op);


/**
 * Generic detile optimised version.
 */
int _detile_generic_opti(const int w, const int h,
                         uint8_t *dst, const int dstLineSize,
                         const uint8_t *src, const int srcLineSize,
                         const int bytesPerPixel,
                         const int subTileWidth, const int subTileHeight,
                         const int tileWidth, const int tileHeight,
                         const int numDirChanges, const struct dirChange *dirChanges);
int detile_generic_opti(const int w, const int h,
                        uint8_t *dst, const int dstLineSize,
                        const uint8_t *src, const int srcLineSize,
                        const struct TileWalk *tw);


#define detile_generic detile_generic_opti
#define fbtiler_generic fbtiler_generic_simple


/**
 * tile/detile demuxers.
 *
 * @param mode the fbtile mode based detiling to call
 * @param arg1 the format_modifier, in case mode is TILE_AUTO
 * @param w width of the image
 * @param h height of the image
 * @param dst the destination image buffer
 * @param dstLineSize the size of each row in dst image, in bytes
 * @param src the source image buffer
 * @param srcLineSize the size of each row in src image, in bytes
 * @param bytesPerPixel the bytes per pixel for the image
 *
 * @return 0 if detiled, 1 if not
 */
int fbtiler_this(enum FBTileMode mode, uint64_t arg1,
                int w, int h,
                uint8_t *dst, int dstLineSize,
                uint8_t *src, int srcLineSize,
                int bytesPerPixel, int op);
int detile_this(enum FBTileMode mode, uint64_t arg1,
                int w, int h,
                uint8_t *dst, int dstLineSize,
                uint8_t *src, int srcLineSize,
                int bytesPerPixel);


int av_frame_copy_with_tiling(AVFrame *dst, enum FBTileMode dstTileMode,
                              AVFrame *src, enum FBTileMode srcTileMode);


/**
 * @}
 */

#endif /* AVUTIL_FBTILE_H */
// vim: set expandtab sts=4: //
