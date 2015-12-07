/*
 * Copyright (c) 2012 Stefano Sabatini
 * Copyright (c) 2015 Andreas Cadhalpun
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * Demuxing and decoding (a codec/format combination) example.
 *
 * This can be useful for fuzz testing.
 * @example ddcf.c
 */

#include <libavutil/avstring.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>

/* needed for decoding video */
static int width, height;
static enum AVPixelFormat pix_fmt;
static uint8_t *video_dst_data[4] = {NULL};
static int      video_dst_linesize[4];
static int video_dst_bufsize;

static int decode_packet(AVCodecContext *dec_ctx, FILE *dst_file, AVFrame *frame, int *got_frame, int *frame_count, AVPacket *pkt)
{
    int ret = -1;
    *got_frame = 0;
    AVSubtitle sub;
    unsigned i, j, k, l;

    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = 0;
        /* decode video frame */
        ret = avcodec_decode_video2(dec_ctx, frame, got_frame, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding video frame (%s)\n", av_err2str(ret));
            return ret;
        }

        if (*got_frame) {

            if (frame->width != width || frame->height != height ||
                frame->format != pix_fmt) {
                fprintf(stderr, "Error: input video width/height/format changed:\n"
                        "old: width = %d, height = %d, format = %s\n"
                        "new: width = %d, height = %d, format = %s\n",
                        width, height, av_get_pix_fmt_name(pix_fmt),
                        dec_ctx->width, dec_ctx->height,
                        av_get_pix_fmt_name(dec_ctx->pix_fmt));
                return -1;
            }

            printf("video_frame n:%d coded_n:%d pts:%s\n",
                   *frame_count, frame->coded_picture_number,
                   av_ts2timestr(frame->pts, &dec_ctx->time_base));

            /* copy decoded frame to destination buffer:
             * this is required since rawvideo expects non aligned data */
            av_image_copy(video_dst_data, video_dst_linesize,
                          (const uint8_t **)(frame->data), frame->linesize,
                          pix_fmt, width, height);
            *frame_count += 1;

            /* write to rawvideo file */
            fwrite(video_dst_data[0], 1, video_dst_bufsize, dst_file);
        }
    } else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        ret = 0;
        /* decode audio frame */
        ret = avcodec_decode_audio4(dec_ctx, frame, got_frame, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding audio frame (%s)\n", av_err2str(ret));
            return ret;
        }
        /* Some audio decoders decode only part of the packet, and have to be
         * called again with the remainder of the packet data.
         * Sample: fate-suite/lossless-audio/luckynight-partial.shn
         * Also, some decoders might over-read the packet. */
        ret = FFMIN(ret, pkt->size);

        if (*got_frame) {
            size_t unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(frame->format);
            printf("audio_frame n:%d nb_samples:%d pts:%s\n",
                   *frame_count, frame->nb_samples,
                   av_ts2timestr(frame->pts, &dec_ctx->time_base));
            *frame_count += 1;

            /* Write the raw audio data samples of the first plane. This works
             * fine for packed formats (e.g. AV_SAMPLE_FMT_S16). However,
             * most audio decoders output planar audio, which uses a separate
             * plane of audio samples for each channel (e.g. AV_SAMPLE_FMT_S16P).
             * In other words, this code will write only the first audio channel
             * in these cases.
             * You should use libswresample or libavfilter to convert the frame
             * to packed data. */
            fwrite(frame->extended_data[0], 1, unpadded_linesize, dst_file);
        }
    } else if (dec_ctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
        ret = 0;
        /* decode video frame */
        ret = avcodec_decode_subtitle2(dec_ctx, &sub, got_frame, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding subtitle (%s)\n", av_err2str(ret));
            return ret;
        }

        if (*got_frame) {

            printf("subtitle n:%d format:%u pts:%s start_time:%u end_time:%u num_recs:%u\n",
                   *frame_count, sub.format,
                   av_ts2timestr(sub.pts, &dec_ctx->time_base),
                   sub.start_display_time, sub.end_display_time, sub.num_rects);

            *frame_count += 1;

            /* write to text file */
            for (i = 0; i < sub.num_rects; i += 1) {
                fprintf(dst_file, "x:%d y:%d w:%d h:%d nb_colors:%d flags:%x linesizes:%d,%d,%d,%d,%d,%d,%d,%d\n"
                        "text:%s\nass:%s\n",
                        sub.rects[i]->x, sub.rects[i]->y, sub.rects[i]->w, sub.rects[i]->h,
                        sub.rects[i]->nb_colors, sub.rects[i]->flags,
                        sub.rects[i]->pict.linesize[0], sub.rects[i]->pict.linesize[1],
                        sub.rects[i]->pict.linesize[2], sub.rects[i]->pict.linesize[3],
                        sub.rects[i]->pict.linesize[4], sub.rects[i]->pict.linesize[5],
                        sub.rects[i]->pict.linesize[6], sub.rects[i]->pict.linesize[7],
                        sub.rects[i]->text, sub.rects[i]->ass);
                for (j = 0; j < AV_NUM_DATA_POINTERS; j += 1) {
                    if (sub.rects[i]->pict.linesize[j]) {
                        fprintf(dst_file, "data:%d\n", j);
                        for (k = 0; k < sub.rects[i]->h; k += 1) {
                            for (l = 0; l < sub.rects[i]->w; l += 1) {
                                fprintf(dst_file, "%x", sub.rects[i]->pict.data[j][l + k * sub.rects[i]->pict.linesize[j]]);
                            }
                            fprintf(dst_file, "\n");
                        }
                    }
                }
            }
            avsubtitle_free(&sub);
        }
    }

    /* de-reference the frame, which is not used anymore */
    if (*got_frame)
        av_frame_unref(frame);

    return ret;
}

static int open_codec_context(AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, char *codec)
{
    int ret = -1;
    AVCodec *dec = avcodec_find_decoder_by_name(codec);
    AVDictionary *opts = NULL;
    unsigned int i;

    for (i = 0; i < fmt_ctx->nb_streams && i < INT_MAX; i += 1) {
        if (fmt_ctx->streams[i]->codec) {
            if (!dec || (fmt_ctx->streams[i]->codec->codec_id == dec->id)) {
                *dec_ctx = fmt_ctx->streams[i]->codec;
                ret = 0;
                break;
            }
        }
    }

    if (ret < 0) {
        fprintf(stderr, "Could not find stream\n");
    } else {
        /* find decoder for the stream */
        if (!dec)
            dec = avcodec_find_decoder((*dec_ctx)->codec_id);
        if (!dec) {
            fprintf(stderr, "Failed to find decoder\n");
            return -1;
        }

        /* Init the decoders, with or without reference counting */
        av_dict_set(&opts, "refcounted_frames", "1", 0);
        av_dict_set(&opts, "strict", "-2", 0);
        av_dict_set(&opts, "codec_whitelist", codec, 0);
        av_dict_set(&opts, "thread_type", "slice", 0);
        if ((ret = avcodec_open2(*dec_ctx, dec, &opts)) < 0) {
            fprintf(stderr, "Failed to open decoder\n");
        }
        av_dict_free(&opts);
    }

    return ret;
}

int main (int argc, char **argv)
{
    int ret = 0;
    const char *src_filename = NULL;
    const char *dst_filename = NULL;
    char* format             = NULL;
    char* codec              = NULL;

    if (argc != 5 && argc != 3) {
        fprintf(stderr, "usage: %s input_file output_file [format codec]\n"
                "API example program to show how to read frames from an input file.\n"
                "This program reads frames from a file, decodes them, and writes decoded\n"
                "frames to a rawvideo/rawaudio file named output_file.\n"
                "Optionally format and codec can be specified.\n\n", argv[0]);
        exit(1);
    }
    src_filename = argv[1];
    dst_filename = argv[2];
    if (argc == 5) {
        format = argv[3];
        codec  = argv[4];
    }

    /* log all debug messages */
    av_log_set_level(AV_LOG_DEBUG);

    /* register all formats and codecs */
    av_register_all();

#ifdef __AFL_HAVE_MANUAL_CONTROL
    while (__AFL_LOOP(1000))
#endif
    {
        AVFormatContext *fmt_ctx = NULL;
        AVInputFormat *fmt       = NULL;
        AVCodecContext *dec_ctx  = NULL;
        FILE *dst_file           = NULL;
        AVFrame *frame           = NULL;
        int got_frame            = 0;
        int frame_count          = 0;
        AVPacket pkt             = { 0 };
        AVDictionary *opts       = NULL;
        ret = 0;
        width = 0;
        height = 0;
        pix_fmt = AV_PIX_FMT_NONE;
        video_dst_bufsize = 0;
        memset(video_dst_data, 0, sizeof(video_dst_data));
        memset(video_dst_linesize, 0, sizeof(video_dst_linesize));

        /* set the whitelists for formats and codecs */
        if (av_dict_set(&opts, "codec_whitelist", codec, 0) < 0) {
            fprintf(stderr, "Could not set codec_whitelist.\n");
            ret = 1;
            goto end;
        }
        if (av_dict_set(&opts, "format_whitelist", format, 0) < 0) {
            fprintf(stderr, "Could not set format_whitelist.\n");
            ret = 1;
            goto end;
        }

        if (format) {
            fmt = av_find_input_format(format);
            if (!fmt) {
                fprintf(stderr, "Could not find input format %s\n", format);
                ret = 1;
                goto end;
            }
        }

        /* open input file, and allocate format context */
        if (avformat_open_input(&fmt_ctx, src_filename, fmt, &opts) < 0) {
            fprintf(stderr, "Could not open source file %s\n", src_filename);
            ret = 1;
            goto end;
        }

        /* retrieve stream information */
        if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
            fprintf(stderr, "Could not find stream information\n");
        }

        /* find stream with specified codec */
        if (open_codec_context(&dec_ctx, fmt_ctx, codec) < 0) {
            fprintf(stderr, "Could not open any stream in input file '%s'\n",
                    src_filename);
            ret = 1;
            goto end;
        }

        /* open output file */
        dst_file = fopen(dst_filename, "wb");
        if (!dst_file) {
            fprintf(stderr, "Could not open destination file %s\n", dst_filename);
            ret = 1;
            goto end;
        }

        if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            /* allocate image where the decoded image will be put */
            width = dec_ctx->width;
            height = dec_ctx->height;
            pix_fmt = dec_ctx->pix_fmt;
            video_dst_bufsize = av_image_alloc(video_dst_data, video_dst_linesize,
                                 width, height, pix_fmt, 1);
            if (video_dst_bufsize < 0) {
                fprintf(stderr, "Could not allocate raw video buffer\n");
                ret = 1;
                goto end;
            }
        }

        /* dump input information to stderr */
        av_dump_format(fmt_ctx, 0, src_filename, 0);

        /* allocate frame */
        frame = av_frame_alloc();
        if (!frame) {
            fprintf(stderr, "Could not allocate frame\n");
            ret = 1;
            goto end;
        }

        printf("Demuxing from file '%s' into '%s'\n", src_filename, dst_filename);

        /* read frames from the file */
        while (av_read_frame(fmt_ctx, &pkt) >= 0) {
            do {
                int decoded = decode_packet(dec_ctx, dst_file, frame, &got_frame, &frame_count, &pkt);
                if (decoded < 0)
                    break;
                /* increase data pointer and decrease size of remaining data buffer */
                pkt.data += decoded;
                pkt.size -= decoded;
            } while (pkt.size > 0);
            av_free_packet(&pkt);
        }

        printf("Flushing cached frames.\n");
        pkt.data = NULL;
        pkt.size = 0;
        do {
            decode_packet(dec_ctx, dst_file, frame, &got_frame, &frame_count, &pkt);
        } while (got_frame);

        printf("Demuxing done.\n");

end:
        /* free allocated memory */
        av_dict_free(&opts);
        avcodec_close(dec_ctx);
        avformat_close_input(&fmt_ctx);
        if (dst_file)
            fclose(dst_file);
        av_frame_free(&frame);
        av_free(video_dst_data[0]);
    }

    return ret;
}
