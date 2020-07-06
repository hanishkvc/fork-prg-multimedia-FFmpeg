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

#include <stdio.h>
#include <stdint.h>

/**
 * @file
 * @brief CPU based Framebuffer tiler detiler
 * @author C Hanish Menon <HanishKVC>
 * @{
 */


// Enable printing of the tile walk
#define DEBUG_FBTILE 1


enum FBTileMode {
    TILE_NONE,
    TILE_AUTO,
    TILE_INTELX,
    TILE_INTELY,
    TILE_INTELYF,
    TILE_INTELGX,
    TILE_INTELGY,
    TILE_NONE_END,
};


/**
 * Map from formatmodifier to fbtile's internal mode.
 *
 * @param formatModifier the format_modifier to map
 * @return the fbtile's equivalent internal mode
 */
#undef DEBUG_FBTILE_FORMATMODIFIER_MAPPING
int fbtilemode_from_formatmodifier(uint64_t formatModifier);


/**
 * Supported pixel formats by the fbtile logics
 */
extern const enum AVPixelFormat fbtilePixFormats[];
/**
 * Check if the given pixel formats are supported by fbtile logic.
 *
 * @param srcPixFormat pixel format of source image
 * @param dstPixFormat pixel format of destination image
 */
int fbtile_checkpixformats(const enum AVPixelFormat srcPixFormat, const enum AVPixelFormat dstPixFormat);


/**
 * Detile legacy intel tile-x layout into linear layout.
 *
 * @param w width of the image
 * @param h height of the image
 * @param dst the destination image buffer
 * @param dstLineSize the size of each row in dst image, in bytes
 * @param src the source image buffer
 * @param srcLineSize the size of each row in src image, in bytes
 */
void detile_intelx(int w, int h,
                          uint8_t *dst, int dstLineSize,
                          const uint8_t *src, int srcLineSize);


/**
 * Detile legacy intel tile-y layout into linear layout.
 *
 * @param w width of the image
 * @param h height of the image
 * @param dst the destination image buffer
 * @param dstLineSize the size of each row in dst image, in bytes
 * @param src the source image buffer
 * @param srcLineSize the size of each row in src image, in bytes
 */
void detile_intely(int w, int h,
                          uint8_t *dst, int dstLineSize,
                          const uint8_t *src, int srcLineSize);


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
 * Settings for Intel Tile-Yf framebuffer layout.
 * May need to swap the 4 pixel wide subtile, have to check doc bit more
 */
extern const int tyfBytesPerPixel;
extern const int tyfSubTileWidth;
extern const int tyfSubTileHeight;
extern const int tyfSubTileWidthBytes;
extern const int tyfTileWidth;
extern const int tyfTileHeight;
extern const int tyfNumDirChanges;
extern struct dirChange tyfDirChanges[];
/**
 * Setting for Intel Tile-X framebuffer layout
 */
extern const int txBytesPerPixel;
extern const int txSubTileWidth;
extern const int txSubTileHeight;
extern const int txSubTileWidthBytes;
extern const int txTileWidth;
extern const int txTileHeight;
extern const int txNumDirChanges;
extern struct dirChange txDirChanges[];
/**
 * Setting for Intel Tile-Y framebuffer layout
 * Even thou a simple generic detiling logic doesnt require the
 * dummy 256 posOffset entry. The pseudo parallel detiling based
 * opti logic requires to know about the Tile boundry.
 */
extern const int tyBytesPerPixel;
extern const int tySubTileWidth;
extern const int tySubTileHeight;
extern const int tySubTileWidthBytes;
extern const int tyTileWidth;
extern const int tyTileHeight;
extern const int tyNumDirChanges;
extern struct dirChange tyDirChanges[];

/**
 * Generic Logic to Detile into linear layout.
 *
 * @param w width of the image
 * @param h height of the image
 * @param dst the destination image buffer
 * @param dstLineSize the size of each row in dst image, in bytes
 * @param src the source image buffer
 * @param srcLineSize the size of each row in src image, in bytes
 * @param bytesPerPixel the bytes per pixel for the image
 * @param subTileWidth the width of subtile within the tile, in pixels
 * @param subTileHeight the height of subtile within the tile, in pixels
 * @param subTileWidthBytes the width of subtile within the tile, in bytes
 * @param tileWidth the width of the tile, in pixels
 * @param tileHeight the height of the tile, in pixels
 */


/**
 * Generic detile simple version, which is fine-grained.
 */
void _detile_generic_simple(const int w, const int h,
                                    uint8_t *dst, const int dstLineSize,
                                    const uint8_t *src, const int srcLineSize,
                                    const int bytesPerPixel,
                                    const int subTileWidth, const int subTileHeight,
                                    const int tileWidth, const int tileHeight,
                                    const int numDirChanges, const struct dirChange *dirChanges);
void detile_generic_simple(const int w, const int h,
                                    uint8_t *dst, const int dstLineSize,
                                    const uint8_t *src, const int srcLineSize,
                                    const struct TileWalk *tw);


/**
 * Generic detile optimised version, minimum subtile supported 4x4.
 */
void _detile_generic_opti(const int w, const int h,
                                uint8_t *dst, const int dstLineSize,
                                const uint8_t *src, const int srcLineSize,
                                const int bytesPerPixel,
                                const int subTileWidth, const int subTileHeight,
                                const int tileWidth, const int tileHeight,
                                const int numDirChanges, const struct dirChange *dirChanges);
void detile_generic_opti(const int w, const int h,
                                uint8_t *dst, const int dstLineSize,
                                const uint8_t *src, const int srcLineSize,
                                const struct TileWalk *tw);


// Use Optimised detile_generic or the Simpler but more fine grained one
#define DETILE_GENERIC_OPTI 1
#ifdef DETILE_GENERIC_OPTI
#define detile_generic detile_generic_opti
#else
#define detile_generic detile_generic_simple
#endif


/**
 * detile demuxer.
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
int detile_this(int mode, uint64_t arg1,
                        int w, int h,
                        uint8_t *dst, int dstLineSize,
                        uint8_t *src, int srcLineSize,
                        int bytesPerPixel);


/**
 * @}
 */

#endif /* AVUTIL_FBTILE_H */
// vim: set expandtab sts=4: //
