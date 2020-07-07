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
 * Tile or Detile the Frame buffer using cpu
 * Currently it supports the following layouts
 *     legacy Intel Tile-X
 *     legacy Intel Tile-Y
 *     newer Intel Tile-Yf
 * It uses the fbtile helper library to do its job.
 * More tiling layouts can be easily supported by adding configuration data
 * for tile walking into fbtile library or its tile|detile_generic function.
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
 *
 * Run Type      : Type   : Seconds Min, Max : TSCCnt Min, Max
 * Non filter run:        :  10.04s, 09.97s  :  00.00M, 00.00M
 * fbdetile=0 run: PasThro:  12.70s, 13.20s  :  00.00M, 00.00M
 * fbdetile=1 run: TileX  :  13.34s, 13.52s  :  06.13M, 06.20M  ; Opti generic
 * fbdetile=2 run: TileY  :  13.59s, 13.68s  :  08.60M, 08.97M  ; Opti generic
 * fbdetile=3 run: TileYf :  13.73s, 13.83s  :  09.82M, 09.92M  ; Opti generic
 * The Older logics
 * fbdetile=2 run: TileX  :  12.45s, 13.41s  :  05.95M, 06.05M  ; prev custom
 * fbdetile=3 run: TileY  :  13.47s, 13.89s  :  06.31M, 06.38M  ; prev custom
 * fbdetile=4 run: TileYf :  13.73s, 13.83s  :  11.41M, 11.83M  ; Simple generic
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/fbtile.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"


// Print time taken by detile using performance counter
#if ARCH_X86
#define DEBUG_PERF 1
#else
#undef DEBUG_PERF
#endif

#ifdef DEBUG_PERF
#include <x86intrin.h>
uint64_t perfTime = 0;
int perfCnt = 0;
#endif

enum FBTileOp {
    FB_TILEOP_NONE,
    FB_TILEOP_TILE,
    FB_TILEOP_DETILE,
    FB_TILEOP_UNKNOWN
};

typedef struct FBDetileContext {
    const AVClass *class;
    int width, height;
    int type;
    int op;
} FBDetileContext;

#define OFFSET(x) offsetof(FBDetileContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption fbdetile_options[] = {
    { "type", "set framebuffer tile|format_modifier conversion type", OFFSET(type), AV_OPT_TYPE_INT, {.i64=TILE_INTELX}, 0, TILE_UNKNOWN-1, FLAGS, "type" },
        { "None", "Linear layout", 0, AV_OPT_TYPE_CONST, {.i64=TILE_NONE}, INT_MIN, INT_MAX, FLAGS, "type" },
        { "intelx", "Intel Tile-X layout", 0, AV_OPT_TYPE_CONST, {.i64=TILE_INTELX}, INT_MIN, INT_MAX, FLAGS, "type" },
        { "intely", "Intel Tile-Y layout", 0, AV_OPT_TYPE_CONST, {.i64=TILE_INTELY}, INT_MIN, INT_MAX, FLAGS, "type" },
        { "intelyf", "Intel Tile-Yf layout", 0, AV_OPT_TYPE_CONST, {.i64=TILE_INTELYF}, INT_MIN, INT_MAX, FLAGS, "type" },
    { "op", "select framebuffer tiling operations i.e tile or detile", OFFSET(op), AV_OPT_TYPE_INT, {.i64=FB_TILEOP_NONE}, 0, FB_TILEOP_UNKNOWN-1, FLAGS, "op" },
        { "None", "Nop", 0, AV_OPT_TYPE_CONST, {.i64=FB_TILEOP_NONE}, INT_MIN, INT_MAX, FLAGS, "op" },
        { "tile", "Apply tiling operation", 0, AV_OPT_TYPE_CONST, {.i64=FB_TILEOP_TILE}, INT_MIN, INT_MAX, FLAGS, "op" },
        { "detile", "Apply detiling operation", 0, AV_OPT_TYPE_CONST, {.i64=FB_TILEOP_DETILE}, INT_MIN, INT_MAX, FLAGS, "op" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(fbdetile);

static av_cold int init(AVFilterContext *ctx)
{
    FBDetileContext *fbdetile = ctx->priv;

    if (fbdetile->op == FB_TILEOP_NONE) {
        av_log(ctx, AV_LOG_INFO, "init:Op: None, Pass through\n");
    } else if (fbdetile->op == FB_TILEOP_TILE) {
        av_log(ctx, AV_LOG_INFO, "init:Op: Apply tiling\n");
    } else if (fbdetile->op == FB_TILEOP_DETILE) {
        av_log(ctx, AV_LOG_INFO, "init:Op: Apply detiling\n");
    } else {
        av_log(ctx, AV_LOG_ERROR, "init:Op: Unknown, shouldnt reach here\n");
    }

    if (fbdetile->type == TILE_NONE) {
        av_log(ctx, AV_LOG_INFO, "init:Type: pass through\n");
    } else if (fbdetile->type == TILE_INTELX) {
        av_log(ctx, AV_LOG_INFO, "init:Type: Intel tile-x\n");
    } else if (fbdetile->type == TILE_INTELY) {
        av_log(ctx, AV_LOG_INFO, "init:Type: Intel tile-y\n");
    } else if (fbdetile->type == TILE_INTELYF) {
        av_log(ctx, AV_LOG_INFO, "init:Type: Intel tile-yf\n");
    } else {
        av_log(ctx, AV_LOG_ERROR, "init: Unknown Tile format specified, shouldnt reach here\n");
    }
    fbdetile->width = 1920;
    fbdetile->height = 1088;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *fmts_list;

    fmts_list = ff_make_format_list(fbtilePixFormats);
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
    av_log(ctx, AV_LOG_INFO, "config_props: %d x %d\n", fbdetile->width, fbdetile->height);

    return 0;
}


static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    FBDetileContext *fbdetile = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;

    if ((fbdetile->op == FB_TILEOP_NONE) || (fbdetile->type == TILE_NONE))
        return ff_filter_frame(outlink, in);

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

#ifdef DEBUG_PERF
    unsigned int tscArg;
    uint64_t perfStart = __rdtscp(&tscArg);
#endif

    if (fbdetile->op == FB_TILEOP_TILE) {
        tile_this(fbdetile->type, 0, fbdetile->width, fbdetile->height,
                        out->data[0], out->linesize[0],
                        in->data[0], in->linesize[0], 4);
    } else {
        detile_this(fbdetile->type, 0, fbdetile->width, fbdetile->height,
                        out->data[0], out->linesize[0],
                        in->data[0], in->linesize[0], 4);
    }

#ifdef DEBUG_PERF
    uint64_t perfEnd = __rdtscp(&tscArg);
    perfTime += (perfEnd - perfStart);
    perfCnt += 1;
#endif

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
#ifdef DEBUG_PERF
    if (perfCnt == 0)
        perfCnt = 1;
    av_log(ctx, AV_LOG_INFO, "uninit:perf: AvgTSCCnt %ld\n", perfTime/perfCnt);
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
    .description   = NULL_IF_CONFIG_SMALL("Tile|Detile Framebuffer using CPU"),
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
