FFmpeg README
=============

# HanishKVC's FFMpeg Fork Notes

## Branch: hkvcFBTilePlusLean

Contains libavutil/fbtile.c|h, which allows tiling transform wrt framebuffer

Contains hwcontext_drm detile support.

Contains hwdownload with fbdetile option, which allows detiling while capturing
live, if requested by user.

Contains fbdetile video filter which allows detiling on already captured content,
as a seperate pass.

KMSGrab GetFB2 for format_modifier

## Branch: hkvcOld20200701_VFFBDeTile

This is the previous standalone version of the fbdetile video filter.

Contains the fbdetile video filter which allows converting tiled layout framebuffers
into linear layout framebuffers.

This is useful for people using kmsgrab and hwdownload, say for example capturing a
wayland session.

Example1

ffmpeg -f kmsgrab -i - -vf "hwdownload,format=bgr0,fbdetile" MyCleanCapture.mp4

Example2

ffmpeg -f kmsgrab -i - -vf "hwdownload,format=bgr0" MyTiledCapture.mp4

ffmpeg -i MyTiledCapture.mp4 -vf "fbdetile" MyCleanCapture.mp4

OR

ffplay -i MyTiledCapture.mp4 -vf "fbdetile"


# Original README

FFmpeg is a collection of libraries and tools to process multimedia content
such as audio, video, subtitles and related metadata.

## Libraries

* `libavcodec` provides implementation of a wider range of codecs.
* `libavformat` implements streaming protocols, container formats and basic I/O access.
* `libavutil` includes hashers, decompressors and miscellaneous utility functions.
* `libavfilter` provides a mean to alter decoded Audio and Video through chain of filters.
* `libavdevice` provides an abstraction to access capture and playback devices.
* `libswresample` implements audio mixing and resampling routines.
* `libswscale` implements color conversion and scaling routines.

## Tools

* [ffmpeg](https://ffmpeg.org/ffmpeg.html) is a command line toolbox to
  manipulate, convert and stream multimedia content.
* [ffplay](https://ffmpeg.org/ffplay.html) is a minimalistic multimedia player.
* [ffprobe](https://ffmpeg.org/ffprobe.html) is a simple analysis tool to inspect
  multimedia content.
* Additional small tools such as `aviocat`, `ismindex` and `qt-faststart`.

## Documentation

The offline documentation is available in the **doc/** directory.

The online documentation is available in the main [website](https://ffmpeg.org)
and in the [wiki](https://trac.ffmpeg.org).

### Examples

Coding examples are available in the **doc/examples** directory.

## License

FFmpeg codebase is mainly LGPL-licensed with optional components licensed under
GPL. Please refer to the LICENSE file for detailed information.

## Contributing

Patches should be submitted to the ffmpeg-devel mailing list using
`git format-patch` or `git send-email`. Github pull requests should be
avoided because they are not part of our review process and will be ignored.
