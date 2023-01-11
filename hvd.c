/*
 * HVD Hardware Video Decoder C library imlementation
 *
 * Copyright 2019-2020 (C) Bartosz Meglicki <meglickib@gmail.com>
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

#include <stdio.h> //fprintf
#include <stdlib.h> //malloc

//internal library data passed around by the user
struct hvd
{
	AVBufferRef* hw_device_ctx;
	enum AVPixelFormat hw_pix_fmt;
	enum AVPixelFormat sw_pix_fmt;
	AVCodecContext* decoder_ctx;
	AVFrame *sw_frame;
	AVFrame *hw_frame;
	AVPacket av_packet;
};

static struct hvd *hvd_close_and_return_null(struct hvd *h, const char *msg, const char *msg_details);
static enum AVPixelFormat hvd_find_pixel_fmt_by_hw_type(const enum AVHWDeviceType type);
static enum AVPixelFormat hvd_get_hw_pix_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts);
static AVFrame *NULL_MSG(const char *msg, const char *msg_details);
static void hvd_dump_sw_pix_formats(struct hvd *h);

//NULL on error
struct hvd *hvd_init(const struct hvd_config *config)
{
	struct hvd *h, zero_hvd = {0};
	enum AVHWDeviceType hardware_type;
	AVCodec *decoder = NULL;
	int err;

	if( ( h = (struct hvd*)malloc(sizeof(struct hvd))) == NULL )
		return hvd_close_and_return_null(NULL, "not enough memory for hvd", NULL);

	*h = zero_hvd; //set all members of dynamically allocated struct to 0 in a portable way

	avcodec_register_all();
	av_log_set_level(AV_LOG_VERBOSE);

	if( (hardware_type = av_hwdevice_find_type_by_name(config->hardware) ) == AV_HWDEVICE_TYPE_NONE )
		return hvd_close_and_return_null(h, "cannot find hardware decoder", config->hardware);

	//This is MUCH easier in FFmpeg 4.0 with avcodec_get_hw_config but we want
	//to support FFmpeg 3.4 (system FFmpeg on Ubuntu 18.04 until 2028).
	if( ( h->hw_pix_fmt = hvd_find_pixel_fmt_by_hw_type(hardware_type) ) == AV_PIX_FMT_NONE)
		return hvd_close_and_return_null(h, "unable to find pixel format for", config->hardware);

	if( ( decoder = avcodec_find_decoder_by_name(config->codec) ) == NULL)
		return hvd_close_and_return_null(h, "cannot find decoder", config->codec);

	if (!(h->decoder_ctx = avcodec_alloc_context3(decoder)))
		return hvd_close_and_return_null(h, "failed to alloc decoder context, no memory?", NULL);

	h->decoder_ctx->width = config->width;
	h->decoder_ctx->height = config->height;

	if(config->profile)
		h->decoder_ctx->profile = config->profile;

	//Set user data carried by AVContext, we need this to determine pixel format
	//from within FFmpeg using our supplied function for decoder_ctx->get_format.
	//This is MUCH easier in FFmpeg 4.0 with avcodec_get_hw_config but we want
	//to support FFmpeg 3.4 (system FFmpeg on Ubuntu 18.04 until 2028).
	h->decoder_ctx->opaque = h;
	h->decoder_ctx->get_format = hvd_get_hw_pix_format;

	//specified device or NULL / empty string for default
	const char *device = (config->device != NULL && config->device[0] != '\0') ? config->device : NULL;

	if ( (err = av_hwdevice_ctx_create(&h->hw_device_ctx, hardware_type, device, NULL, 0) ) < 0)
		return hvd_close_and_return_null(h, "failed to open device and create context for", config->hardware);

	if( (h->decoder_ctx->hw_device_ctx = av_buffer_ref(h->hw_device_ctx) ) == NULL)
		return hvd_close_and_return_null(h, "unable to reference hw_device_ctx", NULL);

	if (( err = avcodec_open2(h->decoder_ctx, decoder, NULL)) < 0)
		return hvd_close_and_return_null(h, "failed to initialize decoder context for", decoder->name);

	//try to find software pixel format that user wants
	if(config->pixel_format == NULL || config->pixel_format[0] == '\0')
		h->sw_pix_fmt = AV_PIX_FMT_NONE;
	else if( ( h->sw_pix_fmt = av_get_pix_fmt(config->pixel_format) ) == AV_PIX_FMT_NONE )
		return hvd_close_and_return_null(h, "failed to find pixel format", config->pixel_format);

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
	case AV_HWDEVICE_TYPE_CUDA:
		fmt = AV_PIX_FMT_CUDA;
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
	struct hvd *h = (struct hvd*)ctx->opaque;

	for (p = pix_fmts; *p != -1; p++)
	{
		if (*p == h->hw_pix_fmt)
			return *p;
	}

	fprintf(stderr, "hvd: failed to get HW surface format\n");
	return AV_PIX_FMT_NONE;
}


void hvd_close(struct hvd* h)
{
	if(h == NULL)
		return;

	av_frame_free(&h->sw_frame);
	av_frame_free(&h->hw_frame);

	avcodec_free_context(&h->decoder_ctx);
	av_buffer_unref(&h->hw_device_ctx);

	free(h);
}

static struct hvd *hvd_close_and_return_null(struct hvd *h, const char *msg, const char *msg_details)
{
	if(msg)
		fprintf(stderr, "hvd: %s %s\n", msg, (msg_details) ? msg_details : "");

	hvd_close(h);

	return NULL;
}

int hvd_send_packet(struct hvd *h,struct hvd_packet *packet)
{
	int err;

	//NULL packet is legal and means user requested flushing
	h->av_packet.data = (packet) ? packet->data : NULL;
	h->av_packet.size = (packet) ? packet->size : 0;

	//WARNING The input buffer, av_packet->data must be AV_INPUT_BUFFER_PADDING_SIZE
	//larger than the actual read bytes because some optimized bitstream readers
	// read 32 or 64 bits at once and could read over the end.
	if ( (err = avcodec_send_packet(h->decoder_ctx, &h->av_packet) ) < 0 )
	{
		fprintf(stderr, "hvd: send_packet error %s\n", av_err2str(err));

		//e.g. non-existing PPS referenced, could not find ref with POC, keep pushing packets
		if(err == AVERROR_INVALIDDATA || err == AVERROR(EIO))
			return HVD_OK;

		//EAGAIN means that we need to read data with avcodec_receive_frame before we can push more data to decoder
		return ( err == AVERROR(EAGAIN) ) ? HVD_AGAIN : HVD_ERROR;
	}

	return HVD_OK;
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
		return NULL_MSG("unable to av_frame_alloc frame", NULL);

	if ( (ret = avcodec_receive_frame(avctx, h->hw_frame) ) < 0 )
	{	//EAGAIN - we need to push more data with avcodec_send_packet
		//EOF  - the decoder was flushed, no more data
		*error = (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) ? HVD_OK : HVD_ERROR;

		//be nice to the user and prepare the decoder for new stream for him
		//if he wants to continue the decoding (startover)
		if(ret == AVERROR_EOF)
			avcodec_flush_buffers(avctx);

		if(*error)
			fprintf(stderr, "hvd: error while decoding - \"%s\"\n", av_err2str(ret));

		return NULL;
	}

	//this would be the place to add fallback to software but we want to treat it as error
	if (h->hw_frame->format != h->hw_pix_fmt)
		return NULL_MSG("frame decoded in software (not in hardware)", NULL);

	// at this point we have a valid frame decoded in hardware
	// try to supply user software frame in the desired format
	h->sw_frame->format=h->sw_pix_fmt;

	if ( (ret = av_hwframe_transfer_data(h->sw_frame, h->hw_frame, 0) ) < 0)
	{
		fprintf(stderr, "hvd: unable to transfer data to system memory - \"%s\"\n", av_err2str(ret));
		hvd_dump_sw_pix_formats(h);
		return NULL;
	}

	*error = HVD_OK;
	return h->sw_frame;
}

static AVFrame *NULL_MSG(const char *msg, const char *msg_details)
{
	if(msg)
		fprintf(stderr, "hvd: %s %s\n", msg, msg_details ? msg_details : "");

	return NULL;
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
