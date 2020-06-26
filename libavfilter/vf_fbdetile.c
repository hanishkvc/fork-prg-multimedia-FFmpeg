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
 * Detile the Frame buffers tile layout using the cpu
 *
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum FilterMode {
    TYPE_INTELX,
    TYPE_INTELY,
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
    { "type", "set framebuffer format_modifier type", OFFSET(type), AV_OPT_TYPE_INT, {.i32=TYPE_INTELX}, 0, NB_TYPE-1, FLAGS, "type" },
        { "intelx", "Intel XTiled layout", 0, AV_OPT_TYPE_CONST, {.i32=TYPE_INTELX}, INT_MIN, INT_MAX, FLAGS, "type" },
        { "intely", "Intel YTiled layout", 0, AV_OPT_TYPE_CONST, {.i32=TYPE_INTELY}, INT_MIN, INT_MAX, FLAGS, "type" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(fbdetile);

static av_cold int init(AVFilterContext *ctx)
{
    FBDetileContext *fbdetile = ctx->priv;

    fbdetile->width = 1920;
    fbdetile->height = 1080;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    const FBDetileContext *fbdetile = ctx->priv;
    static const enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_RGB0, AV_PIX_FMT_0RGB, AV_PIX_FMT_BGR0, AV_PIX_FMT_0BGR, AV_PIX_FMT_NONE};
    AVFilterFormats *fmts_list;

    fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_props(AVFilterLink *inlink)
{
    int p;
    AVFilterContext *ctx = inlink->dst;
    FBDetileContext *fbdetile = ctx->priv;

    fbdetile->width = inlink->w;
    fbdetile->height = inlink->h;
    return 0;
}

static void detile_intelx(AVFilterContext *ctx, int w, int h,
                                uint8_t *dst, int dstLinesize,
                          const uint8_t *src, int srcLinesize)
{
    int i, j;
    // For a 32Bit / Pixel framebuffer
    int tileW = 128;
    int tileH = 8;
    int numHTiles = w/tileW;

    sO = 0;
    dX = 0;
    dY = 0;
    nTRows = (w*h)/tileW;
    cTR = 0;
    k = 0;
    while (cTR < nTRows) {
        dO = dY*dstLinesize + dX;
        memcpy(dst+dO+0*dstLineSize, src+sO+0*512, 512);
        memcpy(dst+dO+1*dstLineSize, src+sO+1*512, 512);
        memcpy(dst+dO+2*dstLineSize, src+sO+2*512, 512);
        memcpy(dst+dO+3*dstLineSize, src+sO+3*512, 512);
        memcpy(dst+dO+4*dstLineSize, src+sO+4*512, 512);
        memcpy(dst+dO+5*dstLineSize, src+sO+5*512, 512);
        memcpy(dst+dO+6*dstLineSize, src+sO+6*512, 512);
        memcpy(dst+dO+7*dstLineSize, src+sO+7*512, 512);
        dX += tileW;
        if (dX > w) {
            dX = 0;
            dY += 8;
        }
        sO = sO + 8*512;
        cTR += 8;
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

    if (fbdetile->type = TYPE_INTELX) {
        detile_intelx(ctx, fbdetile->width, fbdetile->height,
                      out->data[0], out->linesize[0],
                      in->data[0], in->linesize[0]);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{

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
    .description   = NULL_IF_CONFIG_SMALL("Detile Framebuffer"),
    .priv_size     = sizeof(FBDetileContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = fbdetile_inputs,
    .outputs       = fbdetile_outputs,
    .priv_class    = &fbdetile_class,
};
