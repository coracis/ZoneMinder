/*
 * ZoneMinder FFMPEG implementation, $Date$, $Revision$
 * Copyright (C) 2001-2008 Philip Coombes
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/ 

#include "zm_ffmpeg.h"
#include "zm_image.h"
#include "zm_rgb.h"

#if HAVE_LIBAVCODEC || HAVE_LIBAVUTIL || HAVE_LIBSWSCALE

void FFMPEGInit() {
	static bool bInit = false;

	if(!bInit) {
		av_register_all();
		av_log_set_level(AV_LOG_DEBUG);
		bInit = true;
	}
}

#if HAVE_LIBAVUTIL
enum PixelFormat GetFFMPEGPixelFormat(unsigned int p_colours, unsigned p_subpixelorder) {
	enum PixelFormat pf;

	Debug(8,"Colours: %d SubpixelOrder: %d",p_colours,p_subpixelorder);

	switch(p_colours) {
	  case ZM_COLOUR_RGB24:
	  {
	    if(p_subpixelorder == ZM_SUBPIX_ORDER_BGR) {
	      /* BGR subpixel order */
	      pf = PIX_FMT_BGR24;
	    } else {
	      /* Assume RGB subpixel order */
	      pf = PIX_FMT_RGB24;
	    }
	    break;
	  }
	  case ZM_COLOUR_RGB32:
	  {
	    if(p_subpixelorder == ZM_SUBPIX_ORDER_ARGB) {
	      /* ARGB subpixel order */
	      pf = PIX_FMT_ARGB;
	    } else if(p_subpixelorder == ZM_SUBPIX_ORDER_ABGR) {
	      /* ABGR subpixel order */
	      pf = PIX_FMT_ABGR;
	    } else if(p_subpixelorder == ZM_SUBPIX_ORDER_BGRA) {
	      /* BGRA subpixel order */
	      pf = PIX_FMT_BGRA;
	    } else {
	      /* Assume RGBA subpixel order */
	      pf = PIX_FMT_RGBA;
	    }
	    break;
	  }
	  case ZM_COLOUR_GRAY8:
	    pf = PIX_FMT_GRAY8;
	    break;
	  default:
	    Panic("Unexpected colours: %d",p_colours);
	    pf = PIX_FMT_GRAY8; /* Just to shush gcc variable may be unused warning */
	    break;
	}

	return pf;
}
#endif // HAVE_LIBAVUTIL

#if HAVE_LIBSWSCALE && HAVE_LIBAVUTIL
SWScale::SWScale() : gotdefaults(false), swscale_ctx(NULL), input_avframe(NULL), output_avframe(NULL) {
	Debug(4,"SWScale object created");

	/* Allocate AVFrame for the input */
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(56, 1, 0)
	input_avframe = av_frame_alloc();
#else
	input_avframe = avcodec_alloc_frame();
#endif
	if(input_avframe == NULL) {
		Fatal("Failed allocating AVFrame for the input");
	}

	/* Allocate AVFrame for the output */
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(56, 1, 0)
	output_avframe = av_frame_alloc();
#else
	output_avframe = avcodec_alloc_frame();
#endif
	if(output_avframe == NULL) {
		Fatal("Failed allocating AVFrame for the output");
	}
}

SWScale::~SWScale() {

	/* Free up everything */
	av_free(input_avframe);
	input_avframe = NULL;

	av_free(output_avframe);
	output_avframe = NULL;

	if(swscale_ctx) {
		sws_freeContext(swscale_ctx);
		swscale_ctx = NULL;
	}
	
	Debug(4,"SWScale object destroyed");
}

int SWScale::SetDefaults(enum PixelFormat in_pf, enum PixelFormat out_pf, unsigned int width, unsigned int height) {

	/* Assign the defaults */
	default_input_pf = in_pf;
	default_output_pf = out_pf;
	default_width = width;
	default_height = height;

	gotdefaults = true;

	return 0;
}

int SWScale::Convert(const uint8_t* in_buffer, const size_t in_buffer_size, uint8_t* out_buffer, const size_t out_buffer_size, enum PixelFormat in_pf, enum PixelFormat out_pf, unsigned int width, unsigned int height) {
	/* Parameter checking */
	if(in_buffer == NULL || out_buffer == NULL) {
		Error("NULL Input or output buffer");
		return -1;
	}

	if(!width || !height) {
		Error("Invalid width or height");
		return -3;
	}

#if LIBSWSCALE_VERSION_INT >= AV_VERSION_INT(0, 8, 0)
	/* Warn if the input or output pixelformat is not supported */
	if(!sws_isSupportedInput(in_pf)) {
		Warning("swscale does not support the input format: %c%c%c%c",(in_pf)&0xff,((in_pf)&0xff),((in_pf>>16)&0xff),((in_pf>>24)&0xff));
	}
	if(!sws_isSupportedOutput(out_pf)) {
		Warning("swscale does not support the output format: %c%c%c%c",(out_pf)&0xff,((out_pf>>8)&0xff),((out_pf>>16)&0xff),((out_pf>>24)&0xff));
	}
#endif

	/* Check the buffer sizes */
	size_t insize = avpicture_get_size(in_pf, width, height);
	if(insize != in_buffer_size) {
		Error("The input buffer size does not match the expected size for the input format. Required: %d Available: %d", insize, in_buffer_size);
		return -4;
	}
	size_t outsize = avpicture_get_size(out_pf, width, height);
	if(outsize < out_buffer_size) {
		Error("The output buffer is undersized for the output format. Required: %d Available: %d", outsize, out_buffer_size);
		return -5;
	}

	/* Get the context */
	swscale_ctx = sws_getCachedContext( swscale_ctx, width, height, in_pf, width, height, out_pf, SWS_FAST_BILINEAR, NULL, NULL, NULL );
	if(swscale_ctx == NULL) {
		Error("Failed getting swscale context");
		return -6;
	}

	/* Fill in the buffers */
	if(!avpicture_fill( (AVPicture*)input_avframe, (uint8_t*)in_buffer, in_pf, width, height ) ) {
		Error("Failed filling input frame with input buffer");
		return -7;
	}
	if(!avpicture_fill( (AVPicture*)output_avframe, out_buffer, out_pf, width, height ) ) {
		Error("Failed filling output frame with output buffer");
		return -8;
	}

	/* Do the conversion */
	if(!sws_scale(swscale_ctx, input_avframe->data, input_avframe->linesize, 0, height, output_avframe->data, output_avframe->linesize ) ) {
		Error("swscale conversion failed");
		return -10;
	}

	return 0;
}

int SWScale::Convert(const Image* img, uint8_t* out_buffer, const size_t out_buffer_size, enum PixelFormat in_pf, enum PixelFormat out_pf, unsigned int width, unsigned int height) {
	if(img->Width() != width) {
		Error("Source image width differs. Source: %d Output: %d",img->Width(), width);
		return -12;
	}

	if(img->Height() != height) {
		Error("Source image height differs. Source: %d Output: %d",img->Height(), height);
		return -13;
	}

	return Convert(img->Buffer(),img->Size(),out_buffer,out_buffer_size,in_pf,out_pf,width,height);
}

int SWScale::ConvertDefaults(const Image* img, uint8_t* out_buffer, const size_t out_buffer_size) {

	if(!gotdefaults) {
		Error("Defaults are not set");
		return -24;
	}

	return Convert(img,out_buffer,out_buffer_size,default_input_pf,default_output_pf,default_width,default_height);
}

int SWScale::ConvertDefaults(const uint8_t* in_buffer, const size_t in_buffer_size, uint8_t* out_buffer, const size_t out_buffer_size) {

	if(!gotdefaults) {
		Error("Defaults are not set");
		return -24;
	}

	return Convert(in_buffer,in_buffer_size,out_buffer,out_buffer_size,default_input_pf,default_output_pf,default_width,default_height);
}
#endif // HAVE_LIBSWSCALE && HAVE_LIBAVUTIL

#endif // HAVE_LIBAVCODEC || HAVE_LIBAVUTIL || HAVE_LIBSWSCALE

#if HAVE_LIBAVUTIL
int64_t av_rescale_delta(AVRational in_tb, int64_t in_ts,  AVRational fs_tb, int duration, int64_t *last, AVRational out_tb){
    int64_t a, b, this_thing;

    av_assert0(in_ts != AV_NOPTS_VALUE);
    av_assert0(duration >= 0);

    if (*last == AV_NOPTS_VALUE || !duration || in_tb.num*(int64_t)out_tb.den <= out_tb.num*(int64_t)in_tb.den) {
simple_round:
        *last = av_rescale_q(in_ts, in_tb, fs_tb) + duration;
        return av_rescale_q(in_ts, in_tb, out_tb);
    }

    a =  av_rescale_q_rnd(2*in_ts-1, in_tb, fs_tb, AV_ROUND_DOWN)   >>1;
    b = (av_rescale_q_rnd(2*in_ts+1, in_tb, fs_tb, AV_ROUND_UP  )+1)>>1;
    if (*last < 2*a - b || *last > 2*b - a)
        goto simple_round;

   this_thing = av_clip64(*last, a, b);
    *last = this_thing + duration;

    return av_rescale_q(this_thing, fs_tb, out_tb);
}
#endif

int hacked_up_context2_for_older_ffmpeg(AVFormatContext **avctx, AVOutputFormat *oformat, const char *format, const char *filename) {
    AVFormatContext *s = avformat_alloc_context();
    int ret = 0;

    *avctx = NULL;
    if (!s) {
		av_log(s, AV_LOG_ERROR, "Out of memory\n");
		ret = AVERROR(ENOMEM);
		return ret;
    }

    if (!oformat) {
        if (format) {
            oformat = av_guess_format(format, NULL, NULL);
            if (!oformat) {
                av_log(s, AV_LOG_ERROR, "Requested output format '%s' is not a suitable output format\n", format);
                ret = AVERROR(EINVAL);
            }
        } else {
            oformat = av_guess_format(NULL, filename, NULL);
            if (!oformat) {
                ret = AVERROR(EINVAL);
                av_log(s, AV_LOG_ERROR, "Unable to find a suitable output format for '%s'\n", filename);
            }
        }
    }

    if (ret) {
		avformat_free_context(s);
		return ret;
    } else {
		s->oformat = oformat;
		if (s->oformat->priv_data_size > 0) {
			s->priv_data = av_mallocz(s->oformat->priv_data_size);
			if (s->priv_data) {
				if (s->oformat->priv_class) {
					*(const AVClass**)s->priv_data= s->oformat->priv_class;
					av_opt_set_defaults(s->priv_data);
				}
			} else {
				av_log(s, AV_LOG_ERROR, "Out of memory\n");
				ret = AVERROR(ENOMEM);
			   return ret;
			}
			s->priv_data = NULL;
		}

		if (filename) strncpy(s->filename, filename, sizeof(s->filename));
		*avctx = s;
		return 0;
	}
}
