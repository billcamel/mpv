/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "osdep/io.h"

#include "talloc.h"
#include "common/msg.h"
#include "options/options.h"

#include "stream/stream.h"
#include "demux.h"
#include "stheader.h"
#include "codec_tags.h"
#include "mf.h"

#define MF_MAX_FILE_SIZE (1024 * 1024 * 256)

static void demux_seek_mf(demuxer_t *demuxer, double rel_seek_secs, int flags)
{
    mf_t *mf = demuxer->priv;
    int newpos = (flags & SEEK_ABSOLUTE) ? 0 : mf->curr_frame - 1;

    if (flags & SEEK_FACTOR)
        newpos += rel_seek_secs * (mf->nr_of_files - 1);
    else
        newpos += rel_seek_secs * mf->sh->fps;
    if (newpos < 0)
        newpos = 0;
    if (newpos >= mf->nr_of_files)
        newpos = mf->nr_of_files;
    mf->curr_frame = newpos;
}

// return value:
//     0 = EOF or no stream found
//     1 = successfully read a packet
static int demux_mf_fill_buffer(demuxer_t *demuxer)
{
    mf_t *mf = demuxer->priv;
    if (mf->curr_frame >= mf->nr_of_files)
        return 0;

    struct stream *entry_stream = NULL;
    if (mf->streams)
        entry_stream = mf->streams[mf->curr_frame];
    struct stream *stream = entry_stream;
    if (!stream) {
        char *filename = mf->names[mf->curr_frame];
        if (filename)
            stream = stream_open(filename, demuxer->global);
    }

    if (stream) {
        stream_seek(stream, 0);
        bstr data = stream_read_complete(stream, NULL, MF_MAX_FILE_SIZE);
        if (data.len) {
            demux_packet_t *dp = new_demux_packet(data.len);
            if (dp) {
                memcpy(dp->buffer, data.start, data.len);
                dp->pts = mf->curr_frame / mf->sh->fps;
                dp->keyframe = true;
                demux_add_packet(demuxer->streams[0], dp);
            }
        }
        talloc_free(data.start);
    }

    if (stream && stream != entry_stream)
        free_stream(stream);

    mf->curr_frame++;
    return 1;
}

// map file extension/type to a codec name

static const struct {
    const char *type;
    const char *codec;
} type2format[] = {
    { "bmp",            "bmp" },
    { "dpx",            "dpx" },
    { "j2c",            "jpeg2000" },
    { "j2k",            "jpeg2000" },
    { "jp2",            "jpeg2000" },
    { "jpc",            "jpeg2000" },
    { "jpeg",           "mjpeg" },
    { "jpg",            "mjpeg" },
    { "jps",            "mjpeg" },
    { "jls",            "ljpeg" },
    { "thm",            "mjpeg" },
    { "db",             "mjpeg" },
    { "pcx",            "pcx" },
    { "png",            "png" },
    { "pns",            "png" },
    { "ptx",            "ptx" },
    { "tga",            "targa" },
    { "tif",            "tiff" },
    { "tiff",           "tiff" },
    { "sgi",            "sgi" },
    { "sun",            "sunrast" },
    { "ras",            "sunrast" },
    { "rs",             "sunrast" },
    { "ra",             "sunrast" },
    { "im1",            "sunrast" },
    { "im8",            "sunrast" },
    { "im24",           "sunrast" },
    { "im32",           "sunrast" },
    { "sunras",         "sunrast" },
    { "xbm",            "xbm" },
    { "pam",            "pam" },
    { "pbm",            "pbm" },
    { "pgm",            "pgm" },
    { "pgmyuv",         "pgmyuv" },
    { "ppm",            "ppm" },
    { "pnm",            "ppm" },
    { "gif",            "gif" }, // usually handled by demux_lavf
    { "pix",            "brender_pix" },
    { "exr",            "exr" },
    { "pic",            "pictor" },
    { "xface",          "xface" },
    { "xwd",            "xwd" },
    {0}
};

static const char *probe_format(mf_t *mf, char *type, enum demux_check check)
{
    if (check > DEMUX_CHECK_REQUEST)
        return NULL;
    char *org_type = type;
    if (!type || !type[0]) {
        char *p = strrchr(mf->names[0], '.');
        if (p)
            type = p + 1;
    }
    for (int i = 0; type2format[i].type; i++) {
        if (type && strcasecmp(type, type2format[i].type) == 0)
            return type2format[i].codec;
    }
    if (check == DEMUX_CHECK_REQUEST) {
        if (!org_type) {
            MP_ERR(mf, "file type was not set! (try --mf-type=ext)\n");
        } else {
            MP_ERR(mf, "--mf-type set to an unknown codec!\n");
        }
    }
    return NULL;
}

static int demux_open_mf(demuxer_t *demuxer, enum demux_check check)
{
    sh_video_t *sh_video = NULL;
    mf_t *mf;

    if (strncmp(demuxer->stream->url, "mf://", 5) == 0 &&
        demuxer->stream->type == STREAMTYPE_MF)
        mf = open_mf_pattern(demuxer, demuxer->log, demuxer->stream->url + 5);
    else {
        mf = open_mf_single(demuxer, demuxer->log, demuxer->stream->url);
        int bog = 0;
        MP_TARRAY_APPEND(mf, mf->streams, bog, demuxer->stream);
    }

    if (!mf || mf->nr_of_files < 1)
        goto error;

    char *force_type = demuxer->opts->mf_type;
    const char *codec = mp_map_mimetype_to_video_codec(demuxer->stream->mime_type);
    if (!codec || (force_type && force_type[0]))
        codec = probe_format(mf, force_type, check);
    if (!codec)
        goto error;

    mf->curr_frame = 0;

    // create a new video stream header
    struct sh_stream *sh = new_sh_stream(demuxer, STREAM_VIDEO);
    sh_video = sh->video;

    sh->codec = codec;
    sh_video->disp_w = 0;
    sh_video->disp_h = 0;
    sh_video->fps = demuxer->opts->mf_fps;

    mf->sh = sh_video;
    demuxer->priv = (void *)mf;
    demuxer->seekable = true;

    return 0;

error:
    return -1;
}

static void demux_close_mf(demuxer_t *demuxer)
{
}

static int demux_control_mf(demuxer_t *demuxer, int cmd, void *arg)
{
    mf_t *mf = demuxer->priv;

    switch (cmd) {
    case DEMUXER_CTRL_GET_TIME_LENGTH:
        *((double *)arg) = (double)mf->nr_of_files / mf->sh->fps;
        return DEMUXER_CTRL_OK;

    default:
        return DEMUXER_CTRL_NOTIMPL;
    }
}

const demuxer_desc_t demuxer_desc_mf = {
    .name = "mf",
    .desc = "image files (mf)",
    .fill_buffer = demux_mf_fill_buffer,
    .open = demux_open_mf,
    .close = demux_close_mf,
    .seek = demux_seek_mf,
    .control = demux_control_mf,
};
