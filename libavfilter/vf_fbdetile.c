/*
 * Copyright (c) 2020 HanishKVC
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

/**
 * @file
 * Detile the Frame buffer's tile layout using the cpu
 * Currently it supports detiling of following layouts
 *     legacy Intel Tile-X
 *     legacy Intel Tile-Y
 *     newer Intel Tile-Yf
 * More tiling layouts can be easily supported by adding configuration data
 * for the generic detile logic, wrt the required tiling schemes.
 *
 */

/*
 * ToThink|Check: Optimisations
 *
 * Does gcc setting used by ffmpeg allows memcpy | stringops inlining,
 * loop unrolling, better native matching instructions, additional
 * optimisations, ...
 *
 * Does gcc map to optimal memcpy logic, based on the situation it is
 * used in i.e like
 *     based on size of transfer, alignment, architecture, etc
 *     a suitable combination of inlining and or rep movsb and or
 *     simd load/store and or unrolling and or ...
 *
 * If not, may be look at vector_size or intrinsics or appropriate arch
 * and cpu specific inline asm or ...
 *
 */

/*
 * Performance check results on i7-7500u
 * Run Type      : Type   : Seconds Max, Min : TSCCnt Min, Max
 * Non filter run:        :  10.10s, 09.96s  :
 * fbdetile=0 run: TileX  :  13.34s, 13.10s  :  05.95M, 06.00M
 * fbdetile=1 run: TileY  :  13.50s, 13.39s  :  06.22M, 06.32M
 * fbdetile=2 run: TileYf :  13.75s, 13.64s  :  09.66M, 09.84M // Optimised DirChangeList
 * fbdetile=2 run: TileYf :  13.92s, 13.70s  :  12.63M, 13.40M // Raw DirChangeList
 * fbdetile=3 run: TileGX :  13.66s, 13.32s  :  06.16M, 06.33M
 * fbdetile=4 run: TileGY :  13.73s, 13.51s  :  08.40M, 08.63M
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define DEBUG_PERF 1
#ifdef DEBUG_PERF
#include <x86intrin.h>
uint64_t perfTime = 0;
int perfCnt = 0;
#endif

enum FilterMode {
    TYPE_INTELX,
    TYPE_INTELY,
    TYPE_INTELYF,
    TYPE_INTELGX,
    TYPE_INTELGY,
    NB_TYPE
};

typedef struct FBDetileContext {
    const AVClass *class;
    int width, height;
    int type;
} FBDetileContext;

#define OFFSET(x) offsetof(FBDetileContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption fbdetile_options[] = {
    { "type", "set framebuffer format_modifier type", OFFSET(type), AV_OPT_TYPE_INT, {.i64=TYPE_INTELX}, 0, NB_TYPE-1, FLAGS, "type" },
        { "intelx", "Intel Tile-X layout", 0, AV_OPT_TYPE_CONST, {.i64=TYPE_INTELX}, INT_MIN, INT_MAX, FLAGS, "type" },
        { "intely", "Intel Tile-Y layout", 0, AV_OPT_TYPE_CONST, {.i64=TYPE_INTELY}, INT_MIN, INT_MAX, FLAGS, "type" },
        { "intelyf", "Intel Tile-Yf layout", 0, AV_OPT_TYPE_CONST, {.i64=TYPE_INTELYF}, INT_MIN, INT_MAX, FLAGS, "type" },
        { "intelgx", "Intel Tile-X layout, GenericDetile", 0, AV_OPT_TYPE_CONST, {.i64=TYPE_INTELGX}, INT_MIN, INT_MAX, FLAGS, "type" },
        { "intelgy", "Intel Tile-Y layout, GenericDetile", 0, AV_OPT_TYPE_CONST, {.i64=TYPE_INTELGY}, INT_MIN, INT_MAX, FLAGS, "type" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(fbdetile);

static av_cold int init(AVFilterContext *ctx)
{
    FBDetileContext *fbdetile = ctx->priv;

    if (fbdetile->type == TYPE_INTELX) {
        fprintf(stderr,"INFO:fbdetile:init: Intel tile-x to linear\n");
    } else if (fbdetile->type == TYPE_INTELY) {
        fprintf(stderr,"INFO:fbdetile:init: Intel tile-y to linear\n");
    } else if (fbdetile->type == TYPE_INTELYF) {
        fprintf(stderr,"INFO:fbdetile:init: Intel tile-yf to linear\n");
    } else if (fbdetile->type == TYPE_INTELGX) {
        fprintf(stderr,"INFO:fbdetile:init: Intel tile-x to linear, using generic detile\n");
    } else if (fbdetile->type == TYPE_INTELGY) {
        fprintf(stderr,"INFO:fbdetile:init: Intel tile-y to linear, using generic detile\n");
    } else {
        fprintf(stderr,"DBUG:fbdetile:init: Unknown Tile format specified, shouldnt reach here\n");
    }
    fbdetile->width = 1920;
    fbdetile->height = 1080;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    // Currently only RGB based 32bit formats are specified
    // TODO: Technically the logic is transparent to 16bit RGB formats also to a great extent
    static const enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_RGB0, AV_PIX_FMT_0RGB, AV_PIX_FMT_BGR0, AV_PIX_FMT_0BGR,
                                                  AV_PIX_FMT_RGBA, AV_PIX_FMT_ARGB, AV_PIX_FMT_BGRA, AV_PIX_FMT_ABGR,
                                                  AV_PIX_FMT_NONE};
    AVFilterFormats *fmts_list;

    fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    FBDetileContext *fbdetile = ctx->priv;

    fbdetile->width = inlink->w;
    fbdetile->height = inlink->h;
    fprintf(stderr,"DBUG:fbdetile:config_props: %d x %d\n", fbdetile->width, fbdetile->height);

    return 0;
}

static void detile_intelx(AVFilterContext *ctx, int w, int h,
                                uint8_t *dst, int dstLineSize,
                          const uint8_t *src, int srcLineSize)
{
    // Offsets and LineSize are in bytes
    int tileW = 128; // For a 32Bit / Pixel framebuffer, 512/4
    int tileH = 8;

    if (w*4 != srcLineSize) {
        fprintf(stderr,"DBUG:fbdetile:intelx: w%dxh%d, dL%d, sL%d\n", w, h, dstLineSize, srcLineSize);
        fprintf(stderr,"ERRR:fbdetile:intelx: dont support LineSize | Pitch going beyond width\n");
    }
    int sO = 0;
    int dX = 0;
    int dY = 0;
    int nTRows = (w*h)/tileW;
    int cTR = 0;
    while (cTR < nTRows) {
        int dO = dY*dstLineSize + dX*4;
#ifdef DEBUG_FBTILE
        fprintf(stderr,"DBUG:fbdetile:intelx: dX%d dY%d, sO%d, dO%d\n", dX, dY, sO, dO);
#endif
        memcpy(dst+dO+0*dstLineSize, src+sO+0*512, 512);
        memcpy(dst+dO+1*dstLineSize, src+sO+1*512, 512);
        memcpy(dst+dO+2*dstLineSize, src+sO+2*512, 512);
        memcpy(dst+dO+3*dstLineSize, src+sO+3*512, 512);
        memcpy(dst+dO+4*dstLineSize, src+sO+4*512, 512);
        memcpy(dst+dO+5*dstLineSize, src+sO+5*512, 512);
        memcpy(dst+dO+6*dstLineSize, src+sO+6*512, 512);
        memcpy(dst+dO+7*dstLineSize, src+sO+7*512, 512);
        dX += tileW;
        if (dX >= w) {
            dX = 0;
            dY += 8;
        }
        sO = sO + 8*512;
        cTR += 8;
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
static void detile_intely(AVFilterContext *ctx, int w, int h,
                                uint8_t *dst, int dstLineSize,
                          const uint8_t *src, int srcLineSize)
{
    // Offsets and LineSize are in bytes
    int tileW = 4; // For a 32Bit / Pixel framebuffer, 16/4
    int tileH = 32;

    if (w*4 != srcLineSize) {
        fprintf(stderr,"DBUG:fbdetile:intely: w%dxh%d, dL%d, sL%d\n", w, h, dstLineSize, srcLineSize);
        fprintf(stderr,"ERRR:fbdetile:intely: dont support LineSize | Pitch going beyond width\n");
    }
    int sO = 0;
    int dX = 0;
    int dY = 0;
    int nTRows = (w*h)/tileW;
    int cTR = 0;
    while (cTR < nTRows) {
        int dO = dY*dstLineSize + dX*4;
#ifdef DEBUG_FBTILE
        fprintf(stderr,"DBUG:fbdetile:intely: dX%d dY%d, sO%d, dO%d\n", dX, dY, sO, dO);
#endif

        memcpy(dst+dO+0*dstLineSize, src+sO+0*16, 16);
        memcpy(dst+dO+1*dstLineSize, src+sO+1*16, 16);
        memcpy(dst+dO+2*dstLineSize, src+sO+2*16, 16);
        memcpy(dst+dO+3*dstLineSize, src+sO+3*16, 16);
        memcpy(dst+dO+4*dstLineSize, src+sO+4*16, 16);
        memcpy(dst+dO+5*dstLineSize, src+sO+5*16, 16);
        memcpy(dst+dO+6*dstLineSize, src+sO+6*16, 16);
        memcpy(dst+dO+7*dstLineSize, src+sO+7*16, 16);
        memcpy(dst+dO+8*dstLineSize, src+sO+8*16, 16);
        memcpy(dst+dO+9*dstLineSize, src+sO+9*16, 16);
        memcpy(dst+dO+10*dstLineSize, src+sO+10*16, 16);
        memcpy(dst+dO+11*dstLineSize, src+sO+11*16, 16);
        memcpy(dst+dO+12*dstLineSize, src+sO+12*16, 16);
        memcpy(dst+dO+13*dstLineSize, src+sO+13*16, 16);
        memcpy(dst+dO+14*dstLineSize, src+sO+14*16, 16);
        memcpy(dst+dO+15*dstLineSize, src+sO+15*16, 16);
        memcpy(dst+dO+16*dstLineSize, src+sO+16*16, 16);
        memcpy(dst+dO+17*dstLineSize, src+sO+17*16, 16);
        memcpy(dst+dO+18*dstLineSize, src+sO+18*16, 16);
        memcpy(dst+dO+19*dstLineSize, src+sO+19*16, 16);
        memcpy(dst+dO+20*dstLineSize, src+sO+20*16, 16);
        memcpy(dst+dO+21*dstLineSize, src+sO+21*16, 16);
        memcpy(dst+dO+22*dstLineSize, src+sO+22*16, 16);
        memcpy(dst+dO+23*dstLineSize, src+sO+23*16, 16);
        memcpy(dst+dO+24*dstLineSize, src+sO+24*16, 16);
        memcpy(dst+dO+25*dstLineSize, src+sO+25*16, 16);
        memcpy(dst+dO+26*dstLineSize, src+sO+26*16, 16);
        memcpy(dst+dO+27*dstLineSize, src+sO+27*16, 16);
        memcpy(dst+dO+28*dstLineSize, src+sO+28*16, 16);
        memcpy(dst+dO+29*dstLineSize, src+sO+29*16, 16);
        memcpy(dst+dO+30*dstLineSize, src+sO+30*16, 16);
        memcpy(dst+dO+31*dstLineSize, src+sO+31*16, 16);

        dX += tileW;
        if (dX >= w) {
            dX = 0;
            dY += 32;
        }
        sO = sO + 32*16;
        cTR += 32;
    }
}

/*
 * Generic detile logic
 */

struct changeEntry {
    int posOffset;
    int xDelta;
    int yDelta;
};

// Settings for Intel Tile-Yf framebuffer layout
// May need to swap the 4 pixel wide subtile, have to check doc bit more
int yfBytesPerPixel = 4; // Assumes each pixel is 4 bytes
int yfSubTileWidth = 4;
#ifdef RAWDIRCHANGELIST_FORREFERENCE
int yfSubTileHeight = 4;
struct changeEntry yfChanges[] = { {4, 0, 4}, {8, 4, -4}, {16, -4, 4}, {32, 4, -12} };
int yfNumChanges = 4;
#else
int yfSubTileHeight = 8;
struct changeEntry yfChanges[] = { {8, 4, 0}, {16, -4, 8}, {32, 4, -8} };
int yfNumChanges = 3;
#endif
int yfSubTileWidthBytes = 16; //subTileWidth*bytesPerPixel
int yfTileWidth = 16;
int yfTileHeight = 16;
// Setting for Intel Tile-X framebuffer layout
struct changeEntry txChanges[] = { {8, 128, 0} };
int txBytesPerPixel = 4; // Assumes each pixel is 4 bytes
int txSubTileWidth = 128;
int txSubTileHeight = 8;
int txSubTileWidthBytes = 512; //subTileWidth*bytesPerPixel
int txTileWidth = 128;
int txTileHeight = 8;
int txNumChanges = 1;
// Setting for Intel Tile-Y framebuffer layout
// Even thou a simple generic detiling logic doesnt require the
// dummy 256 posOffset entry. A parallel detiling logic requires
// to know about the Tile boundry.
struct changeEntry tyChanges[] = { {32, 4, 0}, {256, 4, 0} };
int tyBytesPerPixel = 4; // Assumes each pixel is 4 bytes
int tySubTileWidth = 4;
int tySubTileHeight = 32;
int tySubTileWidthBytes = 16; //subTileWidth*bytesPerPixel
int tyTileWidth = 32;
int tyTileHeight = 32;
int tyNumChanges = 2;

static void detile_generic_raw(AVFilterContext *ctx, int w, int h,
                                  uint8_t *dst, int dstLineSize,
                            const uint8_t *src, int srcLineSize,
                            int bytesPerPixel,
                            int subTileWidth, int subTileHeight, int subTileWidthBytes, int tileHeight,
                            int numChanges, struct changeEntry *changes)
{

    if (w*bytesPerPixel != srcLineSize) {
        fprintf(stderr,"DBUG:fbdetile:generic: w%dxh%d, dL%d, sL%d\n", w, h, dstLineSize, srcLineSize);
        fprintf(stderr,"ERRR:fbdetile:generic: dont support LineSize | Pitch going beyond width\n");
    }
    int sO = 0;
    int dX = 0;
    int dY = 0;
    int nSTRows = (w*h)/subTileWidth;
    int cSTR = 0;
    while (cSTR < nSTRows) {
        int dO = dY*dstLineSize + dX*bytesPerPixel;
#ifdef DEBUG_FBTILE
        fprintf(stderr,"DBUG:fbdetile:generic: dX%d dY%d, sO%d, dO%d\n", dX, dY, sO, dO);
#endif

        for (int k = 0; k < subTileHeight; k++) {
            memcpy(dst+dO+k*dstLineSize, src+sO+k*subTileWidthBytes, subTileWidthBytes);
        }
        sO = sO + subTileHeight*subTileWidthBytes;

        cSTR += subTileHeight;
        for (int i=numChanges-1; i>=0; i--) {
            if ((cSTR%changes[i].posOffset) == 0) {
                dX += changes[i].xDelta;
                dY += changes[i].yDelta;
		break;
            }
        }
        if (dX >= w) {
            dX = 0;
            dY += tileHeight;
        }
    }
}


static void detile_generic(AVFilterContext *ctx, int w, int h,
                                  uint8_t *dst, int dstLineSize,
                            const uint8_t *src, int srcLineSize,
                            int bytesPerPixel,
                            int subTileWidth, int subTileHeight, int subTileWidthBytes,
                            int tileWidth, int tileHeight,
                            int numChanges, struct changeEntry *changes)
{
    int parallel = 1;

    if (w*bytesPerPixel != srcLineSize) {
        fprintf(stderr,"DBUG:fbdetile:generic: w%dxh%d, dL%d, sL%d\n", w, h, dstLineSize, srcLineSize);
        fprintf(stderr,"ERRR:fbdetile:generic: dont support LineSize | Pitch going beyond width\n");
    }
    if (w%tileWidth != 0) {
        fprintf(stderr,"DBUG:fbdetile:generic:NotSupported: width%d, tileWidth%d\n", w, tileWidth);
    }
    int sO = 0;
    int sOPrev = 0;
    int dX = 0;
    int dY = 0;
    int nSTRows = (w*h)/subTileWidth;
    int nSTRowsInATile = (tileWidth*tileHeight)/subTileWidth;
    int nTilesInARow = w/tileWidth;
    if (nTilesInARow%4 == 0)
        parallel=4;
    else if (nTilesInARow%2 == 0)
        parallel=2;
    else
        parallel=1;
    int cSTR = 0;
    int curTileInRow = 0;
    while (cSTR < nSTRows) {
        int dO = dY*dstLineSize + dX*bytesPerPixel;
#define DEBUG_FBTILE 1
#ifdef DEBUG_FBTILE
        fprintf(stderr,"DBUG:fbdetile:generic: dX%d dY%d, sO%d, dO%d\n", dX, dY, sO, dO);
#endif

	// As most tiling layouts have a minimum subtile of 4x4, if I remember correctly,
	// so this loop could be unrolled to be multiples of 4, and speed up a bit.
	// However if one unrolls to 4 times, then a tiling involving 3x3 or 2x2 wont
	// be handlable. For now leaving it has fully generic.
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

        cSTR += subTileHeight;
        for (int i=numChanges-1; i>=0; i--) {
            if ((cSTR%changes[i].posOffset) == 0) {
                if (i == numChanges-1) {
                    curTileInRow += parallel;
                    dX = curTileInRow*tileWidth;
                    //sO += tileWidth*tileHeight*bytesPerPixel*(parallel-1);
                    sO = sOPrev + tileWidth*tileHeight*bytesPerPixel*(parallel);
                    sOPrev = sO;
                } else {
                    dX += changes[i].xDelta;
                }
                dY += changes[i].yDelta;
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


static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    FBDetileContext *fbdetile = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

#ifdef DEBUG_PERF
    uint64_t perfStart = __rdtsc();
#endif
    if (fbdetile->type == TYPE_INTELX) {
        detile_intelx(ctx, fbdetile->width, fbdetile->height,
                      out->data[0], out->linesize[0],
                      in->data[0], in->linesize[0]);
    } else if (fbdetile->type == TYPE_INTELY) {
        detile_intely(ctx, fbdetile->width, fbdetile->height,
                      out->data[0], out->linesize[0],
                      in->data[0], in->linesize[0]);
    } else if (fbdetile->type == TYPE_INTELYF) {
        detile_generic(ctx, fbdetile->width, fbdetile->height,
                        out->data[0], out->linesize[0],
                        in->data[0], in->linesize[0],
                        yfBytesPerPixel, yfSubTileWidth, yfSubTileHeight, yfSubTileWidthBytes,
                        yfTileWidth, yfTileHeight,
                        yfNumChanges, yfChanges);
    } else if (fbdetile->type == TYPE_INTELGX) {
        detile_generic(ctx, fbdetile->width, fbdetile->height,
                        out->data[0], out->linesize[0],
                        in->data[0], in->linesize[0],
                        txBytesPerPixel, txSubTileWidth, txSubTileHeight, txSubTileWidthBytes,
                        txTileWidth, txTileHeight,
                        txNumChanges, txChanges);
    } else if (fbdetile->type == TYPE_INTELGY) {
        detile_generic(ctx, fbdetile->width, fbdetile->height,
                        out->data[0], out->linesize[0],
                        in->data[0], in->linesize[0],
                        tyBytesPerPixel, tySubTileWidth, tySubTileHeight, tySubTileWidthBytes,
                        tyTileWidth, tyTileHeight,
                        tyNumChanges, tyChanges);
    }
#ifdef DEBUG_PERF
    uint64_t perfEnd = __rdtsc();
    perfTime += (perfEnd - perfStart);
    perfCnt += 1;
#endif

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
#ifdef DEBUG_PERF
    fprintf(stderr, "DBUG:fbdetile:uninit:perf: AvgTSCCnt %ld\n", perfTime/perfCnt);
#endif
}

static const AVFilterPad fbdetile_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad fbdetile_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_fbdetile = {
    .name          = "fbdetile",
    .description   = NULL_IF_CONFIG_SMALL("Detile Framebuffer using CPU"),
    .priv_size     = sizeof(FBDetileContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = fbdetile_inputs,
    .outputs       = fbdetile_outputs,
    .priv_class    = &fbdetile_class,
};

// vim: set expandtab sts=4: //
