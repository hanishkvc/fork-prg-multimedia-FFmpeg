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
 * TileYf, TileGX, TileGY using detile_generic_opti
 *     This mainly impacts TileYf, due to its deeper subtiling
 *     Without opti, its TSCCnt rises to aroun 11.XYM
 * Run Type      : Type   : Seconds Max, Min : TSCCnt Min, Max
 * Non filter run:        :  10.11s, 09.96s  :
 * fbdetile=0 run: TileX  :  13.45s, 13.20s  :  05.95M, 06.10M
 * fbdetile=1 run: TileY  :  13.50s, 13.39s  :  06.22M, 06.39M
 * fbdetile=2 run: TileYf :  13.75s, 13.63s  :  09.82M, 09.90M
 * fbdetile=3 run: TileGX :  13.70s, 13.32s  :  06.15M, 06.24M
 * fbdetile=4 run: TileGY :  14.12s, 13.57s  :  08.75M, 09.10M
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/fbtile.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

// Use Optimised detile_generic or the Simpler but more fine grained one
#define DETILE_GENERIC_OPTI 1
// Enable printing of the tile walk
#undef DEBUG_FBTILE
// Print time taken by detile using performance counter
#define DEBUG_PERF 1

#ifdef DEBUG_PERF
#include <x86intrin.h>
uint64_t perfTime = 0;
int perfCnt = 0;
#endif

typedef struct FBDetileContext {
    const AVClass *class;
    int width, height;
    int type;
} FBDetileContext;

#define OFFSET(x) offsetof(FBDetileContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption fbdetile_options[] = {
    { "type", "set framebuffer format_modifier type", OFFSET(type), AV_OPT_TYPE_INT, {.i64=TILE_INTELX}, 0, TILE_NONE_END-1, FLAGS, "type" },
        { "intelx", "Intel Tile-X layout", 0, AV_OPT_TYPE_CONST, {.i64=TILE_INTELX}, INT_MIN, INT_MAX, FLAGS, "type" },
        { "intely", "Intel Tile-Y layout", 0, AV_OPT_TYPE_CONST, {.i64=TILE_INTELY}, INT_MIN, INT_MAX, FLAGS, "type" },
        { "intelyf", "Intel Tile-Yf layout", 0, AV_OPT_TYPE_CONST, {.i64=TILE_INTELYF}, INT_MIN, INT_MAX, FLAGS, "type" },
        { "intelgx", "Intel Tile-X layout, GenericDetile", 0, AV_OPT_TYPE_CONST, {.i64=TILE_INTELGX}, INT_MIN, INT_MAX, FLAGS, "type" },
        { "intelgy", "Intel Tile-Y layout, GenericDetile", 0, AV_OPT_TYPE_CONST, {.i64=TILE_INTELGY}, INT_MIN, INT_MAX, FLAGS, "type" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(fbdetile);

static av_cold int init(AVFilterContext *ctx)
{
    FBDetileContext *fbdetile = ctx->priv;

    if (fbdetile->type == TILE_INTELX) {
        fprintf(stderr,"INFO:fbdetile:init: Intel tile-x to linear\n");
    } else if (fbdetile->type == TILE_INTELY) {
        fprintf(stderr,"INFO:fbdetile:init: Intel tile-y to linear\n");
    } else if (fbdetile->type == TILE_INTELYF) {
        fprintf(stderr,"INFO:fbdetile:init: Intel tile-yf to linear\n");
    } else if (fbdetile->type == TILE_INTELGX) {
        fprintf(stderr,"INFO:fbdetile:init: Intel tile-x to linear, using generic detile\n");
    } else if (fbdetile->type == TILE_INTELGY) {
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

    detile_this(fbdetile->type, 0, fbdetile->width, fbdetile->height,
                        out->data[0], out->linesize[0],
                        in->data[0], in->linesize[0]);

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
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};

// vim: set expandtab sts=4: //
