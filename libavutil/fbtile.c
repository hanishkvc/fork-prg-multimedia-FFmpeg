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

#include "config.h"
#include "avutil.h"
#include "common.h"
#include "fbtile.h"
#if CONFIG_LIBDRM
#include <drm_fourcc.h>
#endif


int fbtilemode_from_drmformatmodifier(uint64_t formatModifier)
{
    int mode = TILE_NONE_END;

#if CONFIG_LIBDRM
    switch(formatModifier) {
        case DRM_FORMAT_MOD_LINEAR:
            mode = TILE_NONE;
            break;
        case I915_FORMAT_MOD_X_TILED:
            mode = TILE_INTELX;
            break;
        case I915_FORMAT_MOD_Y_TILED:
            mode = TILE_INTELY;
            break;
        case I915_FORMAT_MOD_Yf_TILED:
            mode = TILE_INTELYF;
            break;
        default:
            mode = TILE_NONE_END;
            break;
    }
#endif
#ifdef DEBUG_FBTILE_FORMATMODIFIER_MAPPING
    av_log(NULL, AV_LOG_DEBUG, "fbtile:drmformatmodifier[%lx] mapped to mode[%d]\n", formatModifier, mode);
#endif
    return mode;
}


/**
 * Supported pixel formats
 * Currently only RGB based 32bit formats are specified
 * TODO: Technically the logic is transparent to 16bit RGB formats also to a great extent
 */
const enum AVPixelFormat fbtilePixFormats[] = {AV_PIX_FMT_RGB0, AV_PIX_FMT_0RGB, AV_PIX_FMT_BGR0, AV_PIX_FMT_0BGR,
                                               AV_PIX_FMT_RGBA, AV_PIX_FMT_ARGB, AV_PIX_FMT_BGRA, AV_PIX_FMT_ABGR,
                                               AV_PIX_FMT_NONE};

int fbtile_checkpixformats(const enum AVPixelFormat srcPixFormat, const enum AVPixelFormat dstPixFormat)
{
    int okSrc = 0;
    int okDst = 0;
    for (int i = 0; fbtilePixFormats[i] != AV_PIX_FMT_NONE; i++) {
        if (fbtilePixFormats[i] == srcPixFormat)
            okSrc = 1;
        if (fbtilePixFormats[i] == dstPixFormat)
            okDst = 1;
    }
    return (okSrc && okDst);
}


/*
 * Generic detile logic
 */

/**
 * Settings for Intel Tile-Yf framebuffer layout.
 * May need to swap the 4 pixel wide subtile, have to check doc bit more
 */
struct TileWalk tyfTileWalk = {
                    .bytesPerPixel = 4,
                    .subTileWidth = 4, .subTileHeight = 8,
                    .tileWidth = 32, .tileHeight = 32,
                    .numDirChanges = 6,
                    .dirChanges = { {8, 4, 0}, {16, -4, 8}, {32, 4, -8}, {64, -12, 8}, {128, 4, -24}, {256, 4, -24} }
                };

/**
 * Setting for Intel Tile-X framebuffer layout
 */
struct TileWalk txTileWalk = {
                    .bytesPerPixel = 4,
                    .subTileWidth = 128, .subTileHeight = 8,
                    .tileWidth = 128, .tileHeight = 8,
                    .numDirChanges = 1,
                    .dirChanges = { {8, 128, 0} }
                };

/**
 * Setting for Intel Tile-Y framebuffer layout
 * Even thou a simple generic detiling logic doesnt require the
 * dummy 256 posOffset entry. The pseudo parallel detiling based
 * opti logic requires to know about the Tile boundry.
 */
struct TileWalk tyTileWalk = {
                    .bytesPerPixel = 4,
                    .subTileWidth = 4, .subTileHeight = 32,
                    .tileWidth = 32, .tileHeight = 32,
                    .numDirChanges = 2,
                    .dirChanges = { {32, 4, 0}, {256, 4, 0} }
                };


/**
 * Use _detile_generic_opti in general, available here just for reference
 * and for use in any strange corner case situation, if at all.
 */
void _detile_generic_simple(const int w, const int h,
                                  uint8_t *dst, const int dstLineSize,
                                  const uint8_t *src, const int srcLineSize,
                                  const int bytesPerPixel,
                                  const int subTileWidth, const int subTileHeight,
                                  const int tileWidth, const int tileHeight,
                                  const int numDirChanges, const struct dirChange *dirChanges)
{
    const int subTileWidthBytes = subTileWidth*bytesPerPixel;

    if (w*bytesPerPixel != srcLineSize) {
        av_log(NULL, AV_LOG_ERROR, "fbdetile:genericsimp: w%dxh%d, dL%d, sL%d\n", w, h, dstLineSize, srcLineSize);
        av_log(NULL, AV_LOG_ERROR, "fbdetile:genericsimp: dont support LineSize | Pitch going beyond width\n");
    }
    int sO = 0;
    int dX = 0;
    int dY = 0;
    int nSTLines = (w*h)/subTileWidth;  // numSubTileLines
    int cSTL = 0;                       // curSubTileLine
    while (cSTL < nSTLines) {
        int dO = dY*dstLineSize + dX*bytesPerPixel;
#ifdef DEBUG_FBTILE
        av_log(NULL, AV_LOG_DEBUG, "fbdetile:genericsimp: dX%d dY%d; sO%d, dO%d; %d/%d\n", dX, dY, sO, dO, cSTL, nSTLines);
#endif

        for (int k = 0; k < subTileHeight; k++) {
            memcpy(dst+dO+k*dstLineSize, src+sO+k*subTileWidthBytes, subTileWidthBytes);
        }
        sO = sO + subTileHeight*subTileWidthBytes;

        cSTL += subTileHeight;
        for (int i=numDirChanges-1; i>=0; i--) {
            if ((cSTL%dirChanges[i].posOffset) == 0) {
                dX += dirChanges[i].xDelta;
                dY += dirChanges[i].yDelta;
                break;
            }
        }
        if (dX >= w) {
            dX = 0;
            dY += tileHeight;
        }
    }
}


void detile_generic_simple(const int w, const int h,
                                uint8_t *dst, const int dstLineSize,
                                const uint8_t *src, const int srcLineSize,
                                const struct TileWalk *tw)
{
    _detile_generic_simple(w, h, dst, dstLineSize, src, srcLineSize,
                            tw->bytesPerPixel,
                            tw->subTileWidth, tw->subTileHeight,
                            tw->tileWidth, tw->tileHeight,
                            tw->numDirChanges, tw->dirChanges);
}


void _detile_generic_opti(const int w, const int h,
                                uint8_t *dst, const int dstLineSize,
                                const uint8_t *src, const int srcLineSize,
                                const int bytesPerPixel,
                                const int subTileWidth, const int subTileHeight,
                                const int tileWidth, const int tileHeight,
                                const int numDirChanges, const struct dirChange *dirChanges)
{
    const int subTileWidthBytes = subTileWidth*bytesPerPixel;
    int parallel = 1;

    if (w*bytesPerPixel != srcLineSize) {
        av_log(NULL, AV_LOG_ERROR, "fbdetile:genericopti: w%dxh%d, dL%d, sL%d\n", w, h, dstLineSize, srcLineSize);
        av_log(NULL, AV_LOG_ERROR, "fbdetile:genericopti: dont support LineSize | Pitch going beyond width\n");
    }
    if (w%tileWidth != 0) {
        av_log(NULL, AV_LOG_ERROR, "fbdetile:genericopti:NotSupported:NonMultWidth: width%d, tileWidth%d\n", w, tileWidth);
    }
    int sO = 0;
    int sOPrev = 0;
    int dX = 0;
    int dY = 0;
    int nSTLines = (w*h)/subTileWidth;
    //int nSTLinesInATile = (tileWidth*tileHeight)/subTileWidth;
    int nTilesInARow = w/tileWidth;
    for (parallel=8; parallel>0; parallel--) {
        if (nTilesInARow%parallel == 0)
            break;
    }
    int cSTL = 0;
    int curTileInRow = 0;
    while (cSTL < nSTLines) {
        int dO = dY*dstLineSize + dX*bytesPerPixel;
#ifdef DEBUG_FBTILE
        av_log(NULL, AV_LOG_DEBUG, "fbdetile:genericopti: dX%d dY%d; sO%d, dO%d; %d/%d\n", dX, dY, sO, dO, cSTL, nSTLines);
#endif

        // As most tiling layouts have a minimum subtile of 4x4, if I remember correctly,
        // so this loop can be unrolled to be multiples of 4, and speed up a bit.
        // However tiling involving 3x3 or 2x2 wont be handlable. In which one will have to use
        // detile_generic_simple for such tile layouts.
        // Detile parallely to a limited extent. To avoid any cache set-associativity and or
        // limited cache based thrashing, keep it spacially and inturn temporaly small at one level.
        for (int k = 0; k < subTileHeight; k+=1) {
            for (int p = 0; p < parallel; p++) {
                int pSrcOffset = p*tileWidth*tileHeight*bytesPerPixel;
                int pDstOffset = p*tileWidth*bytesPerPixel;
                memcpy(dst+dO+(k+0)*dstLineSize+pDstOffset, src+sO+(k+0)*subTileWidthBytes+pSrcOffset, subTileWidthBytes);
                /*
                memcpy(dst+dO+(k+1)*dstLineSize+pDstOffset, src+sO+(k+1)*subTileWidthBytes+pSrcOffset, subTileWidthBytes);
                memcpy(dst+dO+(k+2)*dstLineSize+pDstOffset, src+sO+(k+2)*subTileWidthBytes+pSrcOffset, subTileWidthBytes);
                memcpy(dst+dO+(k+3)*dstLineSize+pDstOffset, src+sO+(k+3)*subTileWidthBytes+pSrcOffset, subTileWidthBytes);
                */
            }
        }
        sO = sO + subTileHeight*subTileWidthBytes;

        cSTL += subTileHeight;
        for (int i=numDirChanges-1; i>=0; i--) {
            if ((cSTL%dirChanges[i].posOffset) == 0) {
                if (i == numDirChanges-1) {
                    curTileInRow += parallel;
                    dX = curTileInRow*tileWidth;
                    sO = sOPrev + tileWidth*tileHeight*bytesPerPixel*(parallel);
                    sOPrev = sO;
                } else {
                    dX += dirChanges[i].xDelta;
                }
                dY += dirChanges[i].yDelta;
		break;
            }
        }
        if (dX >= w) {
            dX = 0;
            curTileInRow = 0;
            dY += tileHeight;
            if (dY >= h) {
                break;
            }
        }
    }
}


void detile_generic_opti(const int w, const int h,
                                uint8_t *dst, const int dstLineSize,
                                const uint8_t *src, const int srcLineSize,
                                const struct TileWalk *tw)
{
    _detile_generic_opti(w, h, dst, dstLineSize, src, srcLineSize,
                            tw->bytesPerPixel,
                            tw->subTileWidth, tw->subTileHeight,
                            tw->tileWidth, tw->tileHeight,
                            tw->numDirChanges, tw->dirChanges);
}


int detile_this(int mode, uint64_t arg1,
                        int w, int h,
                        uint8_t *dst, int dstLineSize,
                        uint8_t *src, int srcLineSize,
                        int bytesPerPixel)
{
    static int logState=0;
    if (mode == TILE_AUTO) {
        mode = fbtilemode_from_drmformatmodifier(arg1);
    }
    if (mode == TILE_NONE) {
        return 1;
    }

    if (mode == TILE_INTELX) {
        detile_generic(w, h, dst, dstLineSize, src, srcLineSize, &txTileWalk);
    } else if (mode == TILE_INTELY) {
        detile_generic(w, h, dst, dstLineSize, src, srcLineSize, &tyTileWalk);
    } else if (mode == TILE_INTELYF) {
        detile_generic(w, h, dst, dstLineSize, src, srcLineSize, &tyfTileWalk);
    } else if (mode == TILE_NONE_END) {
        av_log_once(NULL, AV_LOG_WARNING, AV_LOG_VERBOSE, &logState, "fbtile:detile_this:TILE_AUTOOr???: invalid or unsupported format_modifier:%"PRIx64"\n",arg1);
        return 1;
    } else {
        av_log(NULL, AV_LOG_ERROR, "fbtile:detile_this:????: unknown mode specified, check caller\n");
        return 1;
    }
    return 0;
}


// vim: set expandtab sts=4: //
