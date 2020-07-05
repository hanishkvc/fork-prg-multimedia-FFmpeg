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


int fbtilemode_from_formatmodifier(uint64_t formatModifier)
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
    av_log(NULL, AV_LOG_DEBUG, "fbtile:formatmodifier[%lx] mapped to mode[%d]\n", formatModifier, mode);
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


void detile_intelx(int w, int h,
                          uint8_t *dst, int dstLineSize,
                          const uint8_t *src, int srcLineSize)
{
    // Offsets and LineSize are in bytes
    const int pixBytes = 4;                     // bytes per pixel
    const int tileW = 128;                      // tileWidth inPixels, 512/4, For a 32Bits/Pixel framebuffer
    const int tileH = 8;                        // tileHeight inPixelLines
    const int tileWBytes = tileW*pixBytes;      // tileWidth inBytes

    if (w*pixBytes != srcLineSize) {
        av_log(NULL, AV_LOG_ERROR, "fbdetile:intelx: w%dxh%d, dL%d, sL%d\n", w, h, dstLineSize, srcLineSize);
        av_log(NULL, AV_LOG_ERROR, "fbdetile:intelx: dont support LineSize | Pitch going beyond width\n");
    }
    int sO = 0;                 // srcOffset inBytes
    int dX = 0;                 // destX inPixels
    int dY = 0;                 // destY inPixels
    int nTLines = (w*h)/tileW;  // numTileLines; One TileLine = One TileWidth
    int cTL = 0;                // curTileLine
    while (cTL < nTLines) {
        int dO = dY*dstLineSize + dX*pixBytes;
#ifdef DEBUG_FBTILE
        av_log(NULL, AV_LOG_DEBUG, "fbdetile:intelx: dX%d dY%d, sO%d, dO%d\n", dX, dY, sO, dO);
#endif
        memcpy(dst+dO+0*dstLineSize, src+sO+0*tileWBytes, tileWBytes);
        memcpy(dst+dO+1*dstLineSize, src+sO+1*tileWBytes, tileWBytes);
        memcpy(dst+dO+2*dstLineSize, src+sO+2*tileWBytes, tileWBytes);
        memcpy(dst+dO+3*dstLineSize, src+sO+3*tileWBytes, tileWBytes);
        memcpy(dst+dO+4*dstLineSize, src+sO+4*tileWBytes, tileWBytes);
        memcpy(dst+dO+5*dstLineSize, src+sO+5*tileWBytes, tileWBytes);
        memcpy(dst+dO+6*dstLineSize, src+sO+6*tileWBytes, tileWBytes);
        memcpy(dst+dO+7*dstLineSize, src+sO+7*tileWBytes, tileWBytes);
        dX += tileW;
        if (dX >= w) {
            dX = 0;
            dY += tileH;
        }
        sO = sO + tileW*tileH*pixBytes;
        cTL += tileH;
    }
}


/*
 * Intel Legacy Tile-Y layout conversion support
 *
 * currently done in a simple dumb way. Two low hanging optimisations
 * that could be readily applied are
 *
 * a) unrolling the inner for loop
 *    --- Given small size memcpy, should help, DONE
 *
 * b) using simd based 128bit loading and storing along with prefetch
 *    hinting.
 *
 *    TOTHINK|CHECK: Does memcpy already does this and more if situation
 *    is right?!
 *
 *    As code (or even intrinsics) would be specific to each architecture,
 *    avoiding for now. Later have to check if vector_size attribute and
 *    corresponding implementation by gcc can handle different architectures
 *    properly, such that it wont become worse than memcpy provided for that
 *    architecture.
 *
 * Or maybe I could even merge the two intel detiling logics into one, as
 * the semantic and flow is almost same for both logics.
 *
 */
void detile_intely(int w, int h,
                          uint8_t *dst, int dstLineSize,
                          const uint8_t *src, int srcLineSize)
{
    // Offsets and LineSize are in bytes
    const int pixBytes = 4;                 // bytesPerPixel
    // tileW represents subTileWidth here, as it can be repeated to fill a tile
    const int tileW = 4;                    // tileWidth inPixels, 16/4, For a 32Bits/Pixel framebuffer
    const int tileH = 32;                   // tileHeight inPixelLines
    const int tileWBytes = tileW*pixBytes;  // tileWidth inBytes

    if (w*pixBytes != srcLineSize) {
        av_log(NULL, AV_LOG_ERROR, "fbdetile:intely: w%dxh%d, dL%d, sL%d\n", w, h, dstLineSize, srcLineSize);
        av_log(NULL, AV_LOG_ERROR, "fbdetile:intely: dont support LineSize | Pitch going beyond width\n");
    }
    int sO = 0;
    int dX = 0;
    int dY = 0;
    const int nTLines = (w*h)/tileW;
    int cTL = 0;
    while (cTL < nTLines) {
        int dO = dY*dstLineSize + dX*pixBytes;
#ifdef DEBUG_FBTILE
        av_log(NULL, AV_LOG_DEBUG, "fbdetile:intely: dX%d dY%d, sO%d, dO%d\n", dX, dY, sO, dO);
#endif

        memcpy(dst+dO+0*dstLineSize, src+sO+0*tileWBytes, tileWBytes);
        memcpy(dst+dO+1*dstLineSize, src+sO+1*tileWBytes, tileWBytes);
        memcpy(dst+dO+2*dstLineSize, src+sO+2*tileWBytes, tileWBytes);
        memcpy(dst+dO+3*dstLineSize, src+sO+3*tileWBytes, tileWBytes);
        memcpy(dst+dO+4*dstLineSize, src+sO+4*tileWBytes, tileWBytes);
        memcpy(dst+dO+5*dstLineSize, src+sO+5*tileWBytes, tileWBytes);
        memcpy(dst+dO+6*dstLineSize, src+sO+6*tileWBytes, tileWBytes);
        memcpy(dst+dO+7*dstLineSize, src+sO+7*tileWBytes, tileWBytes);
        memcpy(dst+dO+8*dstLineSize, src+sO+8*tileWBytes, tileWBytes);
        memcpy(dst+dO+9*dstLineSize, src+sO+9*tileWBytes, tileWBytes);
        memcpy(dst+dO+10*dstLineSize, src+sO+10*tileWBytes, tileWBytes);
        memcpy(dst+dO+11*dstLineSize, src+sO+11*tileWBytes, tileWBytes);
        memcpy(dst+dO+12*dstLineSize, src+sO+12*tileWBytes, tileWBytes);
        memcpy(dst+dO+13*dstLineSize, src+sO+13*tileWBytes, tileWBytes);
        memcpy(dst+dO+14*dstLineSize, src+sO+14*tileWBytes, tileWBytes);
        memcpy(dst+dO+15*dstLineSize, src+sO+15*tileWBytes, tileWBytes);
        memcpy(dst+dO+16*dstLineSize, src+sO+16*tileWBytes, tileWBytes);
        memcpy(dst+dO+17*dstLineSize, src+sO+17*tileWBytes, tileWBytes);
        memcpy(dst+dO+18*dstLineSize, src+sO+18*tileWBytes, tileWBytes);
        memcpy(dst+dO+19*dstLineSize, src+sO+19*tileWBytes, tileWBytes);
        memcpy(dst+dO+20*dstLineSize, src+sO+20*tileWBytes, tileWBytes);
        memcpy(dst+dO+21*dstLineSize, src+sO+21*tileWBytes, tileWBytes);
        memcpy(dst+dO+22*dstLineSize, src+sO+22*tileWBytes, tileWBytes);
        memcpy(dst+dO+23*dstLineSize, src+sO+23*tileWBytes, tileWBytes);
        memcpy(dst+dO+24*dstLineSize, src+sO+24*tileWBytes, tileWBytes);
        memcpy(dst+dO+25*dstLineSize, src+sO+25*tileWBytes, tileWBytes);
        memcpy(dst+dO+26*dstLineSize, src+sO+26*tileWBytes, tileWBytes);
        memcpy(dst+dO+27*dstLineSize, src+sO+27*tileWBytes, tileWBytes);
        memcpy(dst+dO+28*dstLineSize, src+sO+28*tileWBytes, tileWBytes);
        memcpy(dst+dO+29*dstLineSize, src+sO+29*tileWBytes, tileWBytes);
        memcpy(dst+dO+30*dstLineSize, src+sO+30*tileWBytes, tileWBytes);
        memcpy(dst+dO+31*dstLineSize, src+sO+31*tileWBytes, tileWBytes);

        dX += tileW;
        if (dX >= w) {
            dX = 0;
            dY += tileH;
        }
        sO = sO + tileW*tileH*pixBytes;
        cTL += tileH;
    }
}


/*
 * Generic detile logic
 */

struct TileWalk {
    int bytesPerPixel;
    int subTileWidth, subTileHeight;
    int tileWidth, tileHeight;
    int numDirChanges;
    struct dirChange dirChanges[];
};

/*
 * Direction Change Entry
 * Used to specify the tile walking of subtiles within a tile.
 */
/**
 * Settings for Intel Tile-Yf framebuffer layout.
 * May need to swap the 4 pixel wide subtile, have to check doc bit more
 */
const int tyfBytesPerPixel = 4;
const int tyfSubTileWidth = 4;
const int tyfSubTileHeight = 8;
const int tyfSubTileWidthBytes = tyfSubTileWidth*tyfBytesPerPixel; //16
const int tyfTileWidth = 32;
const int tyfTileHeight = 32;
const int tyfNumDirChanges = 6;
struct dirChange tyfDirChanges[] = { {8, 4, 0}, {16, -4, 8}, {32, 4, -8}, {64, -12, 8 }, {128, 4, -24}, {256, 4, -24} };
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
const int txBytesPerPixel = 4;
const int txSubTileWidth = 128;
const int txSubTileHeight = 8;
const int txSubTileWidthBytes = txSubTileWidth*txBytesPerPixel; //512
const int txTileWidth = 128;
const int txTileHeight = 8;
const int txNumDirChanges = 1;
struct dirChange txDirChanges[] = { {8, 128, 0} };
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
const int tyBytesPerPixel = 4;
const int tySubTileWidth = 4;
const int tySubTileHeight = 32;
const int tySubTileWidthBytes = tySubTileWidth*tyBytesPerPixel; //16
const int tyTileWidth = 32;
const int tyTileHeight = 32;
const int tyNumDirChanges = 2;
struct dirChange tyDirChanges[] = { {32, 4, 0}, {256, 4, 0} };
struct TileWalk tyTileWalk = {
                    .bytesPerPixel = 4,
                    .subTileWidth = 4, .subTileHeight = 32,
                    .tileWidth = 32, .tileHeight = 32,
                    .numDirChanges = 2,
                    .dirChanges = { {32, 4, 0}, {256, 4, 0} }
                };


void detile_generic_simple(int w, int h,
                                  uint8_t *dst, int dstLineSize,
                                  const uint8_t *src, int srcLineSize,
                                  int bytesPerPixel,
                                  int subTileWidth, int subTileHeight, int subTileWidthBytes,
                                  int tileWidth, int tileHeight,
                                  int numDirChanges, struct dirChange *dirChanges)
{

    if (w*bytesPerPixel != srcLineSize) {
        av_log(NULL, AV_LOG_ERROR, "fbdetile:generic: w%dxh%d, dL%d, sL%d\n", w, h, dstLineSize, srcLineSize);
        av_log(NULL, AV_LOG_ERROR, "fbdetile:generic: dont support LineSize | Pitch going beyond width\n");
    }
    int sO = 0;
    int dX = 0;
    int dY = 0;
    int nSTLines = (w*h)/subTileWidth;  // numSubTileLines
    int cSTL = 0;                       // curSubTileLine
    while (cSTL < nSTLines) {
        int dO = dY*dstLineSize + dX*bytesPerPixel;
#ifdef DEBUG_FBTILE
        av_log(NULL, AV_LOG_DEBUG, "fbdetile:generic: dX%d dY%d, sO%d, dO%d\n", dX, dY, sO, dO);
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


void detile_generic_opti(int w, int h,
                                uint8_t *dst, int dstLineSize,
                                const uint8_t *src, int srcLineSize,
                                int bytesPerPixel,
                                int subTileWidth, int subTileHeight, int subTileWidthBytes,
                                int tileWidth, int tileHeight,
                                int numDirChanges, struct dirChange *dirChanges)
{
    int parallel = 1;

    if (w*bytesPerPixel != srcLineSize) {
        av_log(NULL, AV_LOG_ERROR, "fbdetile:generic: w%dxh%d, dL%d, sL%d\n", w, h, dstLineSize, srcLineSize);
        av_log(NULL, AV_LOG_ERROR, "fbdetile:generic: dont support LineSize | Pitch going beyond width\n");
    }
    if (w%tileWidth != 0) {
        av_log(NULL, AV_LOG_ERROR, "fbdetile:generic:NotSupported:NonMultWidth: width%d, tileWidth%d\n", w, tileWidth);
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
        av_log(NULL, AV_LOG_DEBUG, "fbdetile:generic: dX%d dY%d, sO%d, dO%d\n", dX, dY, sO, dO);
#endif

        // As most tiling layouts have a minimum subtile of 4x4, if I remember correctly,
        // so this loop has been unrolled to be multiples of 4, and speed up a bit.
        // However tiling involving 3x3 or 2x2 wont be handlable. Use detile_generic_simple
        // for such tile layouts.
        // Detile parallely to a limited extent. To avoid any cache set-associativity and or
        // limited cache based thrashing, keep it spacially and inturn temporaly small at one level.
        for (int k = 0; k < subTileHeight; k+=4) {
            for (int p = 0; p < parallel; p++) {
                int pSrcOffset = p*tileWidth*tileHeight*bytesPerPixel;
                int pDstOffset = p*tileWidth*bytesPerPixel;
                memcpy(dst+dO+k*dstLineSize+pDstOffset, src+sO+k*subTileWidthBytes+pSrcOffset, subTileWidthBytes);
                memcpy(dst+dO+(k+1)*dstLineSize+pDstOffset, src+sO+(k+1)*subTileWidthBytes+pSrcOffset, subTileWidthBytes);
                memcpy(dst+dO+(k+2)*dstLineSize+pDstOffset, src+sO+(k+2)*subTileWidthBytes+pSrcOffset, subTileWidthBytes);
                memcpy(dst+dO+(k+3)*dstLineSize+pDstOffset, src+sO+(k+3)*subTileWidthBytes+pSrcOffset, subTileWidthBytes);
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


int detile_this(int mode, uint64_t arg1,
                        int w, int h,
                        uint8_t *dst, int dstLineSize,
                        uint8_t *src, int srcLineSize,
                        int bytesPerPixel)
{
    static int logState=0;
    if (mode == TILE_AUTO) {
        mode = fbtilemode_from_formatmodifier(arg1);
    }
    if (mode == TILE_NONE) {
        return 1;
    }

    if (mode == TILE_INTELX) {
        detile_intelx(w, h, dst, dstLineSize, src, srcLineSize);
    } else if (mode == TILE_INTELY) {
        detile_intely(w, h, dst, dstLineSize, src, srcLineSize);
    } else if (mode == TILE_INTELYF) {
        detile_generic(w, h, dst, dstLineSize, src, srcLineSize,
                            tyfBytesPerPixel, tyfSubTileWidth, tyfSubTileHeight, tyfSubTileWidthBytes,
                            tyfTileWidth, tyfTileHeight,
                            tyfNumDirChanges, tyfDirChanges);
    } else if (mode == TILE_INTELGX) {
        detile_generic(w, h, dst, dstLineSize, src, srcLineSize,
                            txBytesPerPixel, txSubTileWidth, txSubTileHeight, txSubTileWidthBytes,
                            txTileWidth, txTileHeight,
                            txNumDirChanges, txDirChanges);
    } else if (mode == TILE_INTELGY) {
        detile_generic(w, h, dst, dstLineSize, src, srcLineSize,
                            tyBytesPerPixel, tySubTileWidth, tySubTileHeight, tySubTileWidthBytes,
                            tyTileWidth, tyTileHeight,
                            tyNumDirChanges, tyDirChanges);
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
