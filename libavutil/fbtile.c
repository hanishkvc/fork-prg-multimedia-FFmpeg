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


enum FBTileMode fbtilemode_from_drmformatmodifier(uint64_t formatModifier)
{
    enum FBTileMode mode = TILE_UNKNOWN;

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
            mode = TILE_UNKNOWN;
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
    int errSrc = 1;
    int errDst = 1;
    for (int i = 0; fbtilePixFormats[i] != AV_PIX_FMT_NONE; i++) {
        if (fbtilePixFormats[i] == srcPixFormat)
            errSrc = 0;
        if (fbtilePixFormats[i] == dstPixFormat)
            errDst = 0;
    }
    return (errSrc | errDst);
}


/*
 * Generic detile logic
 * The tile layout data is assumed to be tightly packed, with no gaps inbetween.
 * However the logic does try to accomodate a destination linear layout memory,
 * where there is possibly some additional bytes beyond the width in each line
 * of pixel data.
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
 * _fbtiler_generic_simple to tile a linear layout
 */
#define OP_TILE 1
int _fbtiler_generic_simple(const int w, const int h,
                         uint8_t *dst, const int dstLineSize,
                         const uint8_t *src, const int srcLineSize,
                         const int bytesPerPixel,
                         const int subTileWidth, const int subTileHeight,
                         const int tileWidth, const int tileHeight,
                         const int numDirChanges, const struct dirChange *dirChanges,
                         int op)
{
    uint8_t *tld, *lin;
    int tldLineSize, linLineSize;
    const int subTileWidthBytes = subTileWidth*bytesPerPixel;

    if (op == OP_TILE) {
        lin = src;
        linLineSize = srcLineSize;
        tld = dst;
        tldLineSize = dstLineSize;
    } else {
        tld = src;
        tldLineSize = srcLineSize;
        lin = dst;
        linLineSize = dstLineSize;
    }

    // To keep things sane and simple tile layout is assumed to be tightly packed,
    // so below check is a indirect logical assumption, even thou tldLineSize is not directly mappable at one level
    if (w*bytesPerPixel != tldLineSize) {
        av_log(NULL, AV_LOG_ERROR, "fbtile:genericsimp: w%dxh%d, dL%d, sL%d\n", w, h, tldLineSize, linLineSize);
        av_log(NULL, AV_LOG_ERROR, "fbtile:genericsimp: dont support tldLineSize | Pitch going beyond width\n");
        return FBT_ERR;
    }
    int tO = 0;
    int lX = 0;
    int lY = 0;
    int nSTLines = (w*h)/subTileWidth;  // numSubTileLines
    int cSTL = 0;                       // curSubTileLine
    while (cSTL < nSTLines) {
        int lO = lY*linLineSize + lX*bytesPerPixel;
#ifdef DEBUG_FBTILE
        av_log(NULL, AV_LOG_DEBUG, "fbtile:genericsimp: lX%d lY%d; lO%d, tO%d; %d/%d\n", lX, lY, lO, tO, cSTL, nSTLines);
#endif

        if (op == OP_TILE) {
            for (int k = 0; k < subTileHeight; k++) {
                memcpy(tld+tO+k*subTileWidthBytes, lin+lO+k*linLineSize, subTileWidthBytes);
            }
        } else {
            for (int k = 0; k < subTileHeight; k++) {
                memcpy(lin+lO+k*linLineSize, tld+tO+k*subTileWidthBytes, subTileWidthBytes);
            }
        }
        tO = tO + subTileHeight*subTileWidthBytes;

        cSTL += subTileHeight;
        for (int i=numDirChanges-1; i>=0; i--) {
            if ((cSTL%dirChanges[i].posOffset) == 0) {
                lX += dirChanges[i].xDelta;
                lY += dirChanges[i].yDelta;
                break;
            }
        }
        if (lX >= w) {
            lX = 0;
            lY += tileHeight;
        }
    }
    return FBT_OK;
}


int fbtiler_generic_simple(const int w, const int h,
                          uint8_t *dst, const int dstLineSize,
                          const uint8_t *src, const int srcLineSize,
                          const struct TileWalk *tw, int op)
{
    return _fbtiler_generic_simple(w, h, dst, dstLineSize, src, srcLineSize,
                                  tw->bytesPerPixel,
                                  tw->subTileWidth, tw->subTileHeight,
                                  tw->tileWidth, tw->tileHeight,
                                  tw->numDirChanges, tw->dirChanges, op);
}


int _detile_generic_opti(const int w, const int h,
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
        av_log(NULL, AV_LOG_ERROR, "fbdetile:genericopti: dont support srcLineSize | Pitch going beyond width\n");
        return FBT_ERR;
    }
    if (w%tileWidth != 0) {
        av_log(NULL, AV_LOG_ERROR, "fbdetile:genericopti:NotSupported:Width being non-mult Of TileWidth: width%d, tileWidth%d\n", w, tileWidth);
        return FBT_ERR;
    }
    int sO = 0;
    int sOPrev = 0;
    int dX = 0;
    int dY = 0;
    int nTilesInARow = w/tileWidth;
    for (parallel=8; parallel>0; parallel--) {
        if (nTilesInARow%parallel == 0)
            break;
    }
    int nSTLines = (w*h)/subTileWidth;  // numSubTileLines
    int cSTL = 0;                       // curSubTileLine
    int curTileInRow = 0;
    while (cSTL < nSTLines) {
        int dO = dY*dstLineSize + dX*bytesPerPixel;
#ifdef DEBUG_FBTILE
        av_log(NULL, AV_LOG_DEBUG, "fbdetile:genericopti: dX%d dY%d; sO%d, dO%d; %d/%d\n", dX, dY, sO, dO, cSTL, nSTLines);
#endif

        // As most tiling layouts have a minimum subtile of 4x4, if I remember correctly,
        // so this loop can be unrolled to be multiples of 4, and speed up a bit.
        // However tiling involving 3x3 or 2x2 wont be handlable. In which one will have to use
        // detile_generic_simple for such tile layouts. So not unrolling for now.
        // Detile parallely to a limited extent. Gain some speed by reusing calcs, but still avoid
        // any cache set-associativity and or limited cache based thrashing. Keep it spatially and
        // inturn temporaly small at one level.
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
    return FBT_OK;
}


int detile_generic_opti(const int w, const int h,
                        uint8_t *dst, const int dstLineSize,
                        const uint8_t *src, const int srcLineSize,
                        const struct TileWalk *tw)
{
    return _detile_generic_opti(w, h, dst, dstLineSize, src, srcLineSize,
                                tw->bytesPerPixel,
                                tw->subTileWidth, tw->subTileHeight,
                                tw->tileWidth, tw->tileHeight,
                                tw->numDirChanges, tw->dirChanges);
}


int fbtiler_this(enum FBTileMode mode, uint64_t arg1,
                        int w, int h,
                        uint8_t *dst, int dstLineSize,
                        uint8_t *src, int srcLineSize,
                        int bytesPerPixel, int op)
{
    if (mode == TILE_NONE) {
        av_log(NULL, AV_LOG_WARNING, "fbtiler:tile_this:TILE_NONE: not tiling\n");
        return FBT_ERR;
    }

    if (mode == TILE_INTELX) {
        return fbtiler_generic(w, h, dst, dstLineSize, src, srcLineSize, &txTileWalk, op);
    } else if (mode == TILE_INTELY) {
        return fbtiler_generic(w, h, dst, dstLineSize, src, srcLineSize, &tyTileWalk, op);
    } else if (mode == TILE_INTELYF) {
        return fbtiler_generic(w, h, dst, dstLineSize, src, srcLineSize, &tyfTileWalk, op);
    } else {
        av_log(NULL, AV_LOG_WARNING, "fbtiler:tile_this:%d: unknown mode specified, not tiling\n", mode);
        return FBT_ERR;
    }
    return FBT_ERR;
}


int detile_this(enum FBTileMode mode, uint64_t arg1,
                        int w, int h,
                        uint8_t *dst, int dstLineSize,
                        uint8_t *src, int srcLineSize,
                        int bytesPerPixel)
{
    if (mode == TILE_NONE) {
        av_log(NULL, AV_LOG_WARNING, "fbtile:detile_this:TILE_NONE: not detiling\n");
        return FBT_ERR;
    }

    if (mode == TILE_INTELX) {
        return detile_generic(w, h, dst, dstLineSize, src, srcLineSize, &txTileWalk);
    } else if (mode == TILE_INTELY) {
        return detile_generic(w, h, dst, dstLineSize, src, srcLineSize, &tyTileWalk);
    } else if (mode == TILE_INTELYF) {
        return detile_generic(w, h, dst, dstLineSize, src, srcLineSize, &tyfTileWalk);
    } else {
        av_log(NULL, AV_LOG_WARNING, "fbtile:detile_this:%d: unknown mode specified, not detiling\n", mode);
        return FBT_ERR;
    }
    return FBT_ERR;
}


int av_frame_copy_with_tiling(AVFrame *dst, enum FBTileMode dstTileMode, AVFrame *src, enum FBTileMode srcTileMode)
{
    int err;

    if (dstTileMode == TILE_NONE) {         // i.e DeTile
        err = fbtile_checkpixformats(src->format, dst->format);
        if (!err) {
            err = detile_this(srcTileMode, 0, dst->width, dst->height,
                              dst->data[0], dst->linesize[0],
                              src->data[0], src->linesize[0], 4);
            if (!err) {
                return FBT_OK;
            }
        }
    } else if (srcTileMode == TILE_NONE) {  // i.e Tile
        err = fbtile_checkpixformats(src->format, dst->format);
        if (!err) {
            err = fbtiler_this(dstTileMode, 0, src->width, src->height,
                              dst->data[0], dst->linesize[0],
                              src->data[0], src->linesize[0], 4, 1);
            if (!err) {
                return FBT_OK;
            }
        }
    }
    return av_frame_copy(dst, src);
}


// vim: set expandtab sts=4: //
