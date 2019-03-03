/*
 * HVD Hardware Video Decoder C library imlementation
 *
 * Copyright 2019 (C) Bartosz Meglicki <meglickib@gmail.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include "hvd.h"

// FFmpeg
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>

// FFmpeg avformat
#include <libavformat/avformat.h>


#include <stdio.h> //fprintf
#include <stdlib.h> //malloc

//more temp
#include <libavcodec/jni.h>

#include <android/log.h>
#include <jni.h>


JavaVM	*g_vm;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* aReserved)
{
	g_vm = vm;
	//JNIEnv* env;
   // if ( vm->GetEnv( (void**)(&env), JNI_VERSION_1_6 ) != JNI_OK )
   //     return -1;

    // Get jclass with env->FindClass.
    // Register methods with env->RegisterNatives.
	__android_log_write(ANDROID_LOG_DEBUG, "hvd", "JNI ON LOAD\n");

	return JNI_VERSION_1_6;
}

//internal library data passed around by the user
struct hvd
{
	AVBufferRef* hw_device_ctx;
	enum AVPixelFormat hw_pix_fmt;
	enum AVPixelFormat sw_pix_fmt;
	AVCodecContext *decoder_ctx;
	AVFormatContext *input_ctx;
	int video_stream;
	AVFrame *sw_frame;
	AVFrame *hw_frame;
	AVPacket av_packet;
};

static struct hvd *hvd_close_and_return_null(struct hvd *h);
static enum AVPixelFormat hvd_find_pixel_fmt_by_hw_type(const enum AVHWDeviceType type);
static enum AVPixelFormat hvd_get_hw_pix_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts);
static void hvd_dump_sw_pix_formats(struct hvd *h);


void custom_log(void *ptr, int level, const char* fmt, va_list vl)
{
	__android_log_vprint(ANDROID_LOG_DEBUG,"hvd", fmt, vl);
}

//NULL on error
struct hvd *hvd_init(const struct hvd_config *config)
{
	struct hvd *h, zero_hvd = {0};
	enum AVHWDeviceType hardware_type;
	AVCodec *decoder = NULL, *stream_decoder = NULL;
	int err;
	int ret;
	AVStream *video = NULL;

	JNIEnv *jni_env = 0;

	int getEnvStat = (*g_vm)->GetEnv(g_vm,(void**) &jni_env, JNI_VERSION_1_6);

    if (getEnvStat == JNI_EDETACHED)
	{
		__android_log_print(ANDROID_LOG_DEBUG, "hvd", "getenv not attached");

		jint result=(*g_vm)->AttachCurrentThread(g_vm, &jni_env, NULL);
		__android_log_print(ANDROID_LOG_DEBUG, "hvd", "JNI_OK is %d\n", JNI_OK);
    }
	else if (getEnvStat == JNI_OK)
	{//
		__android_log_print(ANDROID_LOG_DEBUG, "hvd", "already attached\n", JNI_OK);
    }
	else if (getEnvStat == JNI_EVERSION)
		__android_log_print(ANDROID_LOG_DEBUG, "hvd", "get env version not supported");

	av_jni_set_java_vm(g_vm, NULL);

	if( ( h = (struct hvd*)malloc(sizeof(struct hvd))) == NULL )
	{
		fprintf(stderr, "hvd: not enough memory for hvd\n");
		return NULL;
	}

	*h = zero_hvd; //set all members of dynamically allocated struct to 0 in a portable way

	avcodec_register_all();
	av_log_set_level(AV_LOG_TRACE);
	av_log_set_callback(custom_log);

/*
	if( (hardware_type = av_hwdevice_find_type_by_name(config->hardware) ) == AV_HWDEVICE_TYPE_NONE )
	{
		fprintf(stderr, "hvd: cannot find hardware decoder %s\n", config->hardware);
		return hvd_close_and_return_null(h);
	}
*/

/*
	//This is MUCH easier in FFmpeg 4.0 with avcodec_get_hw_config but we want
	//to support FFmpeg 3.4 (system FFmpeg on Ubuntu 18.04 until 2028).
	if( ( h->hw_pix_fmt = hvd_find_pixel_fmt_by_hw_type(hardware_type) ) == AV_PIX_FMT_NONE)
	{
		fprintf(stderr, "hvd: unable to find pixel format for %s\n", config->hardware);
		return hvd_close_and_return_null(h);
	}
*/

	if( ( decoder=avcodec_find_decoder_by_name(config->codec) ) == NULL)
	{
		fprintf(stderr, "hvd: cannot find decoder %s\n", config->codec);
		return hvd_close_and_return_null(h);
	}

	if (!(h->decoder_ctx = avcodec_alloc_context3(decoder)))
	{
		fprintf(stderr, "hvd: failed to alloc decoder context, no memory?\n");
		return hvd_close_and_return_null(h);
	}

	//Set user data carried by AVContext, we need this to determine pixel format
	//from within FFmpeg using our supplied function for decoder_ctx->get_format.
	//This is MUCH easier in FFmpeg 4.0 with avcodec_get_hw_config but we want
	//to support FFmpeg 3.4 (system FFmpeg on Ubuntu 18.04 until 2028).
	h->decoder_ctx->opaque = h;
	//h->decoder_ctx->get_format = hvd_get_hw_pix_format;

	//specified device or NULL / empty string for default
//	const char *device = (config->device != NULL && config->device[0] != '\0') ? config->device : NULL;
/*
	//DEVICE -> NULL
	if ( (err = av_hwdevice_ctx_create(&h->hw_device_ctx, hardware_type, NULL, NULL, 0) ) < 0)
	{
		fprintf(stderr, "hvd: failed to create %s device.\n", config->hardware);
		return hvd_close_and_return_null(h);
	}
*/

/*
	if( (h->decoder_ctx->hw_device_ctx = av_buffer_ref(h->hw_device_ctx) ) == NULL)
	{
		fprintf(stderr, "hvd: unable to reference hw_device_ctx.\n");
		return hvd_close_and_return_null(h);
	}
*/
	/* open the input file */
	// !!! USE device as file name for test
    if (avformat_open_input(&h->input_ctx, config->device, NULL, NULL) != 0)
	{
        fprintf(stderr, "hvd: cannot open input file '%s'\n", config->device);
		return hvd_close_and_return_null(h);
    }

    if (avformat_find_stream_info(h->input_ctx, NULL) < 0)
	{
        fprintf(stderr, "hvd: cannot find input stream information.\n");
		return hvd_close_and_return_null(h);
    }

    /* find the video stream information */
    ret = av_find_best_stream(h->input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &stream_decoder, 0);
    if (ret < 0)
	{
        fprintf(stderr, "hvd: cannot find a video stream in the input file\n");
        return hvd_close_and_return_null(h);
    }
	h->video_stream=ret;

	video = h->input_ctx->streams[h->video_stream];

    if (avcodec_parameters_to_context(h->decoder_ctx, video->codecpar) < 0)
	{
		fprintf(stderr, "hvd: cannot find a video stream in the input file\n");
		return hvd_close_and_return_null(h);
	}

	if (( err = avcodec_open2(h->decoder_ctx, decoder, NULL)) < 0)
	{
		fprintf(stderr, "hvd: failed to initialize decoder context for %s, error %s\n", decoder->name, av_err2str(err));
		return hvd_close_and_return_null(h);
	}

	//try to find software pixel format that user wants
	if(config->pixel_format == NULL || config->pixel_format[0] == '\0')
		h->sw_pix_fmt = AV_PIX_FMT_NONE;
	else if( ( h->sw_pix_fmt = av_get_pix_fmt(config->pixel_format) ) == AV_PIX_FMT_NONE )
	{
		fprintf(stderr, "hvd: failed to find pixel format %s\n", config->pixel_format);
		return hvd_close_and_return_null(h);
	}

	av_init_packet(&h->av_packet);
	h->av_packet.data = NULL;
	h->av_packet.size = 0;

	return h;
}

// To be replaced in FFmpeg 4.0 with avcodec_get_hw_config. This is necessary for FFmpeg 3.4.
// This is clumsy - we need to hardcode device type to its internal pixel format.
// If device type is not on this list it will not be supported by the library.
// (extend this list as much as possible, this is the only thing which is holding support for some hardware)
static enum AVPixelFormat hvd_find_pixel_fmt_by_hw_type(const enum AVHWDeviceType type)
{
	enum AVPixelFormat fmt;

	switch (type) {
	case AV_HWDEVICE_TYPE_VAAPI:
		fmt = AV_PIX_FMT_VAAPI;
		break;
	case AV_HWDEVICE_TYPE_DXVA2:
		fmt = AV_PIX_FMT_DXVA2_VLD;
		break;
	case AV_HWDEVICE_TYPE_D3D11VA:
		fmt = AV_PIX_FMT_D3D11;
		break;
	case AV_HWDEVICE_TYPE_VDPAU:
		fmt = AV_PIX_FMT_VDPAU;
		break;
	case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:
		fmt = AV_PIX_FMT_VIDEOTOOLBOX;
		break;
	case AV_HWDEVICE_TYPE_MEDIACODEC:
		fmt = AV_PIX_FMT_MEDIACODEC;
		break;
	default:
		fmt = AV_PIX_FMT_NONE;
		break;
	}

	return fmt;
}

static enum AVPixelFormat hvd_get_hw_pix_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
	const enum AVPixelFormat *p;
	struct hvd *h=(struct hvd*)ctx->opaque;

	for (p = pix_fmts; *p != -1; p++)
	{
		//temp
		fprintf(stderr, "hvd: checking format %d %s\n", *p, av_get_pix_fmt_name(*p));

		if (*p == h->hw_pix_fmt)
			return *p;
	}

	fprintf(stderr, "hvd: failed to get HW surface format.\n");
	return AV_PIX_FMT_NONE;
}


void hvd_close(struct hvd* h)
{
	if(h == NULL)
		return;

	av_frame_free(&h->sw_frame);
	av_frame_free(&h->hw_frame);

	avformat_close_input(&h->input_ctx);

	avcodec_free_context(&h->decoder_ctx);
	av_buffer_unref(&h->hw_device_ctx);

	free(h);
}

static struct hvd *hvd_close_and_return_null(struct hvd *h)
{
	hvd_close(h);
	return NULL;
}

int hvd_send_packet(struct hvd *h,struct hvd_packet *packet)
{
	int err;

	av_packet_unref(&h->av_packet);

	if ((err = av_read_frame(h->input_ctx, &h->av_packet)) < 0)
	{
		fprintf(stderr, "hvd: failed to read frame with av_read_frame %d\n", err);
		return HVD_ERROR;
	}
	if (h->av_packet.stream_index != h->video_stream)
		return HVD_OK;

	if ( (err = avcodec_send_packet(h->decoder_ctx, &h->av_packet) ) < 0 )
	{
		fprintf(stderr, "hvd: send_packet error %d\n", err);
		//EAGAIN means that we need to read data with avcodec_receive_frame before we can push more data to decoder
		return ( err == AVERROR(EAGAIN) ) ? HVD_AGAIN : HVD_ERROR;
	}

	return HVD_OK;

	/*
	int err;

	if(packet)
	{
		h->av_packet.data=packet->data;
		h->av_packet.size=packet->size;
	}
	else
	{	//user requested flushing
		h->av_packet.data = NULL;
		h->av_packet.size = 0;
	}

	//WARNING The input buffer, av_packet->data must be AV_INPUT_BUFFER_PADDING_SIZE
	//larger than the actual read bytes because some optimized bitstream readers
	// read 32 or 64 bits at once and could read over the end.
	if ( (err = avcodec_send_packet(h->decoder_ctx, &h->av_packet) ) < 0 )
	{
		fprintf(stderr, "hvd: send_packet error %d\n", err);
		//EAGAIN means that we need to read data with avcodec_receive_frame before we can push more data to decoder
		return ( err == AVERROR(EAGAIN) ) ? HVD_AGAIN : HVD_ERROR;
	}

	return HVD_OK;
	*/
}


//returns:
//- non NULL on success
//- NULL and error == HVD_OK if more data is needed or flushed completely
//- NULL and error == HVD_ERROR if error occured
//the ownership of returned AVFrame* remains with the library
AVFrame *hvd_receive_frame(struct hvd *h, int *error)
{
	AVCodecContext *avctx=h->decoder_ctx;
	int ret = 0;

	*error = HVD_ERROR;
	//free the leftovers from the last call (if any)
	//this will happen here or in hvd_close, whichever is first
	av_frame_free(&h->hw_frame);
	av_frame_free(&h->sw_frame);

	if ( !( h->hw_frame = av_frame_alloc() ) || !( h->sw_frame = av_frame_alloc() ) )
	{
		fprintf(stderr, "hvd: unable to av_frame_alloc frame\n");
		return NULL;
	}

	if ( (ret = avcodec_receive_frame(avctx, h->hw_frame) ) < 0 )
	{	//EAGAIN - we need to push more data with avcodec_send_packet
		//EOF  - the decoder was flushed, no more data
		*error = (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) ? HVD_OK : HVD_ERROR;

		//be nice to the user and prepare the decoder for new stream for him
		//if he wants to continue the decoding (startover)
		if(ret == AVERROR_EOF)
			avcodec_flush_buffers(avctx);

		if(*error)
			fprintf(stderr, "hvd: error while decoding %d\n", ret);
		return NULL;
	}
/*
	if (h->hw_frame->format != h->hw_pix_fmt)
	{	//this would be the place to add fallback to software but we want to treat it as error
		fprintf(stderr, "hvd: frame decoded in software (not in hardware)\n");
		return NULL;
	}
	// at this point we have a valid frame decoded in hardware
	// try to supply user software frame in the desired format
	h->sw_frame->format=h->sw_pix_fmt;

	if ( (ret = av_hwframe_transfer_data(h->sw_frame, h->hw_frame, 0) ) < 0)
	{
		fprintf(stderr, "hvd: unable to transfer data to system memory - \"%s\"\n", av_err2str(ret));
		hvd_dump_sw_pix_formats(h);
		return NULL;
	}
*/
	fprintf(stderr, "hvd: decoded frame with pixel format %d %s\n", h->hw_frame->format, av_get_pix_fmt_name(h->hw_frame->format));

	*error=HVD_OK;

	__android_log_print(ANDROID_LOG_DEBUG, "nhvd", "decoded in format %s\n", av_get_pix_fmt_name(h->hw_frame->format));


	//TEMP
	return h->hw_frame;

	//return h->sw_frame;
}

static void hvd_dump_sw_pix_formats(struct hvd *h)
{
	enum AVPixelFormat *formats, *iterator;

	if(av_hwframe_transfer_get_formats(h->hw_frame->hw_frames_ctx, AV_HWFRAME_TRANSFER_DIRECTION_FROM, &formats, 0) < 0)
	{
		fprintf(stderr, "hvd: failed to get transfer formats\n");
		return;
	}
	iterator=formats;

	fprintf(stderr, "hvd: make sure you are using supported software pixel format:\n");

	while(*iterator != AV_PIX_FMT_NONE)
	{
		fprintf(stderr, "%d : %s\n", *iterator, av_get_pix_fmt_name (*iterator));
		++iterator;
	}

	av_free(formats);
}
