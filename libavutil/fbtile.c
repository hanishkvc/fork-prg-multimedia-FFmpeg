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

#include "config.h"
#include "avutil.h"
#include "common.h"
#include "fbtile.h"
#if CONFIG_LIBDRM
#include <drm_fourcc.h>
#endif


SCOPEIN enum FBTileLayout ff_fbtile_getlayoutid(enum FFFBTileFamily family, uint64_t familyTileType)
{
    enum FBTileLayout layout = FBTILE_UNKNOWN;

    switch(family) {
    case FF_FBTILE_FAMILY_DRM:
#if CONFIG_LIBDRM
        switch(familyTileType) {
        case DRM_FORMAT_MOD_LINEAR:
            layout = FBTILE_NONE;
            break;
        case I915_FORMAT_MOD_X_TILED:
            layout = FBTILE_INTEL_XGEN9;
            break;
        case I915_FORMAT_MOD_Y_TILED:
            layout = FBTILE_INTEL_YGEN9;
            break;
        case I915_FORMAT_MOD_Yf_TILED:
            layout = FBTILE_INTEL_YF;
            break;
        default:
            layout = FBTILE_UNKNOWN;
            break;
        }
#else
        av_log(NULL, AV_LOG_WARNING, "fbtile:getlayoutid: family[%d] familyTileType[%ld]\n", family, familyTileType);
#endif
        break;
    default:
        av_log(NULL, AV_LOG_WARNING, "fbtile:getlayoutid: unknown family[%d] familyTileType[%ld]\n", family, familyTileType);
    }
    av_log(NULL, AV_LOG_VERBOSE, "fbtile:getlayoutid: family[%d] familyTileType[%ld] maps to layoutid[%d]\n", family, familyTileType, layout);
    return layout;
}


/**
 * Supported pixel formats
 * Currently only RGB based 32bit formats are specified
 * TODO: Technically the logic is transparent to 16bit RGB formats also to a great extent
 */
SCOPEIN const enum AVPixelFormat fbtilePixFormats[] = {
                                        AV_PIX_FMT_RGB0, AV_PIX_FMT_0RGB, AV_PIX_FMT_BGR0, AV_PIX_FMT_0BGR,
                                        AV_PIX_FMT_RGBA, AV_PIX_FMT_ARGB, AV_PIX_FMT_BGRA, AV_PIX_FMT_ABGR,
                                        AV_PIX_FMT_NONE};

SCOPEIN int fbtile_checkpixformats(const enum AVPixelFormat srcPixFormat, const enum AVPixelFormat dstPixFormat)
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
 * Generic tile/detile logic
 * The tile layout data is assumed to be tightly packed, with no gaps inbetween.
 * However the logic does try to accomodate a src/dst linear layout memory,
 * where there is possibly some additional bytes beyond the width in each line
 * of pixel data.
 */

/**
 * Settings for Intel Tile-Yf framebuffer layout.
 * May need to swap the 4 pixel wide subtile, have to check doc bit more
 */
static struct FBTileWalk tyfTileWalk = {
                    .bytesPerPixel = 4,
                    .subTileWidth = 4, .subTileHeight = 8,
                    .tileWidth = 32, .tileHeight = 32,
                    .numDirChanges = 6,
                    .dirChanges = { {8, 4, 0}, {16, -4, 8}, {32, 4, -8}, {64, -12, 8}, {128, 4, -24}, {256, 4, -24} }
                };

/**
 * Setting for Intel Tile-X framebuffer layout
 */
static struct FBTileWalk txTileWalk = {
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
static struct FBTileWalk tyTileWalk = {
                    .bytesPerPixel = 4,
                    .subTileWidth = 4, .subTileHeight = 32,
                    .tileWidth = 32, .tileHeight = 32,
                    .numDirChanges = 2,
                    .dirChanges = { {32, 4, 0}, {256, 4, 0} }
                };


/**
 * _fbtile_generic_simple tile/detile layout
 */
static int _fbtile_generic_simple(enum FFFBTileOps op,
                                   const int w, const int h,
                                   uint8_t *dst, const int dstLineSize,
                                   uint8_t *src, const int srcLineSize,
                                   const int bytesPerPixel,
                                   const int subTileWidth, const int subTileHeight,
                                   const int tileWidth, const int tileHeight,
                                   const int numDirChanges, const struct FBTWDirChange *dirChanges)
{
    int tO, lO;
    int lX, lY;
    int cSTL, nSTLines;
    uint8_t *tld, *lin;
    int tldLineSize, linLineSize;
    const int subTileWidthBytes = subTileWidth*bytesPerPixel;

    if (op == FF_FBTILE_OPS_TILE) {
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
    tO = 0;
    lX = 0;
    lY = 0;
    nSTLines = (w*h)/subTileWidth;  // numSubTileLines
    cSTL = 0;                       // curSubTileLine
    while (cSTL < nSTLines) {
        lO = lY*linLineSize + lX*bytesPerPixel;
#ifdef DEBUG_FBTILE
        av_log(NULL, AV_LOG_DEBUG, "fbtile:genericsimp: lX%d lY%d; lO%d, tO%d; %d/%d\n", lX, lY, lO, tO, cSTL, nSTLines);
#endif

        for (int k = 0; k < subTileHeight; k++) {
            if (op == FF_FBTILE_OPS_TILE) {
                memcpy(tld+tO+k*subTileWidthBytes, lin+lO+k*linLineSize, subTileWidthBytes);
            } else {
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


SCOPEIN int fbtile_generic_simple(enum FFFBTileOps op,
                           const int w, const int h,
                           uint8_t *dst, const int dstLineSize,
                           uint8_t *src, const int srcLineSize,
                           const struct FBTileWalk *tw)
{
    return _fbtile_generic_simple(op, w, h,
                                   dst, dstLineSize, src, srcLineSize,
                                   tw->bytesPerPixel,
                                   tw->subTileWidth, tw->subTileHeight,
                                   tw->tileWidth, tw->tileHeight,
                                   tw->numDirChanges, tw->dirChanges);
}


static int _fbtile_generic_opti(enum FFFBTileOps op,
                                 const int w, const int h,
                                 uint8_t *dst, const int dstLineSize,
                                 uint8_t *src, const int srcLineSize,
                                 const int bytesPerPixel,
                                 const int subTileWidth, const int subTileHeight,
                                 const int tileWidth, const int tileHeight,
                                 const int numDirChanges, const struct FBTWDirChange *dirChanges)
{
    int tO, lO, tOPrev;
    int lX, lY;
    int cSTL, nSTLines;
    int curTileInRow, nTilesInARow;
    uint8_t *tld, *lin;
    int tldLineSize, linLineSize;
    const int subTileWidthBytes = subTileWidth*bytesPerPixel;
    int parallel = 1;

    if (op == FF_FBTILE_OPS_TILE) {
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

    if (w*bytesPerPixel != tldLineSize) {
        av_log(NULL, AV_LOG_ERROR, "fbtile:genericopti: w%dxh%d, dL%d, sL%d\n", w, h, linLineSize, tldLineSize);
        av_log(NULL, AV_LOG_ERROR, "fbtile:genericopti: dont support tldLineSize | Pitch going beyond width\n");
        return FBT_ERR;
    }
    if (w%tileWidth != 0) {
        av_log(NULL, AV_LOG_ERROR, "fbtile:genericopti:NotSupported:Width being non-mult Of TileWidth: width%d, tileWidth%d\n", w, tileWidth);
        return FBT_ERR;
    }
    tO = 0;
    tOPrev = 0;
    lX = 0;
    lY = 0;
    nTilesInARow = w/tileWidth;
    for (parallel=8; parallel>0; parallel--) {
        if (nTilesInARow%parallel == 0)
            break;
    }
    nSTLines = (w*h)/subTileWidth;  // numSubTileLines
    cSTL = 0;                       // curSubTileLine
    curTileInRow = 0;
    while (cSTL < nSTLines) {
        lO = lY*linLineSize + lX*bytesPerPixel;
#ifdef DEBUG_FBTILE
        av_log(NULL, AV_LOG_DEBUG, "fbtile:genericopti: lX%d lY%d; tO%d, lO%d; %d/%d\n", lX, lY, tO, lO, cSTL, nSTLines);
#endif

        // As many tiling layouts have subtile and walk sizes which are multiples of 4,
        // so this loop has been unrolled to be multiples of 4, and speed up a bit.
        // If this condition is not satisfied, esp along vert dir, then use fbtile_generic_simple.
        // (De)tile parallely and gain some speed by allowing reuse of some calcs and parallelism.
        if (op == FF_FBTILE_OPS_DETILE) {
            for (int k = 0; k < subTileHeight; k+=4) {
                for (int p = 0; p < parallel; p++) {
                    int pTldOffset = p*tileWidth*tileHeight*bytesPerPixel;
                    int pLinOffset = p*tileWidth*bytesPerPixel;
                    memcpy(lin+lO+(k+0)*linLineSize+pLinOffset, tld+tO+(k+0)*subTileWidthBytes+pTldOffset, subTileWidthBytes);
                    memcpy(lin+lO+(k+1)*linLineSize+pLinOffset, tld+tO+(k+1)*subTileWidthBytes+pTldOffset, subTileWidthBytes);
                    memcpy(lin+lO+(k+2)*linLineSize+pLinOffset, tld+tO+(k+2)*subTileWidthBytes+pTldOffset, subTileWidthBytes);
                    memcpy(lin+lO+(k+3)*linLineSize+pLinOffset, tld+tO+(k+3)*subTileWidthBytes+pTldOffset, subTileWidthBytes);
                }
            }
        } else {
            for (int k = 0; k < subTileHeight; k+=4) {
                for (int p = 0; p < parallel; p++) {
                    int pTldOffset = p*tileWidth*tileHeight*bytesPerPixel;
                    int pLinOffset = p*tileWidth*bytesPerPixel;
                    memcpy(tld+tO+(k+0)*subTileWidthBytes+pTldOffset, lin+lO+(k+0)*linLineSize+pLinOffset, subTileWidthBytes);
                    memcpy(tld+tO+(k+1)*subTileWidthBytes+pTldOffset, lin+lO+(k+1)*linLineSize+pLinOffset, subTileWidthBytes);
                    memcpy(tld+tO+(k+2)*subTileWidthBytes+pTldOffset, lin+lO+(k+2)*linLineSize+pLinOffset, subTileWidthBytes);
                    memcpy(tld+tO+(k+3)*subTileWidthBytes+pTldOffset, lin+lO+(k+3)*linLineSize+pLinOffset, subTileWidthBytes);
                }
            }
        }

        tO = tO + subTileHeight*subTileWidthBytes;
        cSTL += subTileHeight;

        for (int i=numDirChanges-1; i>=0; i--) {
            if ((cSTL%dirChanges[i].posOffset) == 0) {
                if (i == numDirChanges-1) {
                    curTileInRow += parallel;
                    lX = curTileInRow*tileWidth;
                    tO = tOPrev + tileWidth*tileHeight*bytesPerPixel*(parallel);
                    tOPrev = tO;
                } else {
                    lX += dirChanges[i].xDelta;
                }
                lY += dirChanges[i].yDelta;
		break;
            }
        }
        if (lX >= w) {
            lX = 0;
            curTileInRow = 0;
            lY += tileHeight;
            if (lY >= h) {
                break;
            }
        }
    }
    return FBT_OK;
}


SCOPEIN int fbtile_generic_opti(enum FFFBTileOps op,
                         const int w, const int h,
                         uint8_t *dst, const int dstLineSize,
                         uint8_t *src, const int srcLineSize,
                         const struct FBTileWalk *tw)
{
    return _fbtile_generic_opti(op, w, h,
                                 dst, dstLineSize, src, srcLineSize,
                                 tw->bytesPerPixel,
                                 tw->subTileWidth, tw->subTileHeight,
                                 tw->tileWidth, tw->tileHeight,
                                 tw->numDirChanges, tw->dirChanges);
}


static int fbtile_conv(enum FFFBTileOps op, enum FBTileLayout layout,
                 int w, int h,
                 uint8_t *dst, int dstLineSize,
                 uint8_t *src, int srcLineSize,
                 int bytesPerPixel)
{
    static int logStateNone = 0;
    static int logStateUnknown = 0;

    switch(layout) {
    case FBTILE_NONE:
        av_log_once(NULL, AV_LOG_WARNING, AV_LOG_VERBOSE, &logStateNone, "fbtile:conv:FBTILE_NONE: not (de)tiling\n");
        return FBT_ERR;
    case FBTILE_INTEL_XGEN9:
        return fbtile_generic_opti(op, w, h, dst, dstLineSize, src, srcLineSize, &txTileWalk);
    case FBTILE_INTEL_YGEN9:
        return fbtile_generic_opti(op, w, h, dst, dstLineSize, src, srcLineSize, &tyTileWalk);
    case FBTILE_INTEL_YF:
        return fbtile_generic_opti(op, w, h, dst, dstLineSize, src, srcLineSize, &tyfTileWalk);
    default:
        av_log_once(NULL, AV_LOG_WARNING, AV_LOG_VERBOSE, &logStateUnknown, "fbtile:conv: unknown layout [%d] specified, not (de)tiling\n", layout);
        return FBT_ERR;
    }
}


/*
 * Copy one AVFrame into the other, tiling or detiling as required, if possible.
 * NOTE: Either the Source or the Destination AVFrame (i.e one of them) should be linear.
 * NOTE: If the tiling layout is not understood, it will do a simple copy.
 */
SCOPEIN int fbtile_frame_copy(AVFrame *dst, enum FBTileLayout dstTileLayout, AVFrame *src, enum FBTileLayout srcTileLayout,
                              enum FBTileFrameCopyStatus *status)
{
    int err;

    if (dstTileLayout == FBTILE_NONE) {         // i.e DeTile
        err = fbtile_checkpixformats(src->format, dst->format);
        if (!err) {
            err = fbtile_conv(FF_FBTILE_OPS_DETILE, srcTileLayout,
                                dst->width, dst->height,
                                dst->data[0], dst->linesize[0],
                                src->data[0], src->linesize[0], 4);
            if (!err) {
                *status = FBTILE_FRAMECOPY_TILECOPY;
                return FBT_OK;
            }
        }
    } else if (srcTileLayout == FBTILE_NONE) {  // i.e Tile
        err = fbtile_checkpixformats(src->format, dst->format);
        if (!err) {
            err = fbtile_conv(FF_FBTILE_OPS_TILE, dstTileLayout,
                                src->width, src->height,
                                dst->data[0], dst->linesize[0],
                                src->data[0], src->linesize[0], 4);
            if (!err) {
                *status = FBTILE_FRAMECOPY_TILECOPY;
                return FBT_OK;
            }
        }
    } else {
        av_log(NULL, AV_LOG_WARNING, "fbtile:framecopy: both src [%d] and dst [%d] layouts cant be tiled\n", srcTileLayout, dstTileLayout);
    }
    *status = FBTILE_FRAMECOPY_COPYONLY;
    return av_frame_copy(dst, src);
}


// vim: set expandtab sts=4: //
