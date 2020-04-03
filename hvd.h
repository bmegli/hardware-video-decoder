/*
 * HVD Hardware Video Decoder C library header
 *
 * Copyright 2019-2020 (C) Bartosz Meglicki <meglickib@gmail.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

/**
 ******************************************************************************
 *
 *  \mainpage HVD documentation
 *  \see https://github.com/bmegli/hardware-video-decoder
 *
 *  \copyright  Copyright (C) 2019 Bartosz Meglicki
 *  \file       hvd.h
 *  \brief      Library public interface header
 *
 ******************************************************************************
 */

#ifndef HVD_H
#define HVD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>

/** \addtogroup interface Public interface
 *  @{
 */

/**
 * @struct hvd
 * @brief Internal library data passed around by the user.
 * @see hvd_init, hvd_close
 */
struct hvd;

/**
 * @struct hvd_config
 * @brief Decoder configuration.
 *
 * The hardware can be one of:
 * - vaapi
 * - vdpau
 * - dxva2
 * - d3d11va
 * - videotoolbox
 *
 * The device can be:
 * - NULL (select automatically)
 * - point to valid device e.g. "/dev/dri/renderD128" for vaapi
 *
 * The codec (should be supported by your hardware):
 * - h264
 * - hevc
 * - vp8
 * - vp9
 * - ...
 *
 * The pixel_format is format you want to receive data in.
 * Only hardware conversions are supported. If you select
 * something unsupported by hardware, the library will dump for you
 * list of supported pixel formats to standard error. From my experience
 * even those reported are not supported in all scenarios.
 *
 * Typical examples:
 * - nv12
 * - yuv420p (this is generally safe choice)
 * - rgb0
 * - bgr0
 * - ...
 *
 * For pixel format explanation see:
 * <a href="https://ffmpeg.org/doxygen/3.4/pixfmt_8h.html#a9a8e335cf3be472042bc9f0cf80cd4c5">FFmpeg pixel formats</a>
 *
 * You typically don't have to specify width, height and profile (leave as 0) but some codecs need this information.
 *
 * For width and height the decoder may overwrite your values while parsing the data.
 * It is not safe to assume that width/height of decoded frame matches what you supplied.
 *
 * For possible profiles see:
 * <a href="https://ffmpeg.org/doxygen/3.4/avcodec_8h.html#ab424d258655424e4b1690e2ab6fcfc66">FFmpeg profiles</a>
 *
 * For H.264 profile can typically be:
 * - FF_PROFILE_H264_CONSTRAINED_BASELINE
 * - FF_PROFILE_H264_MAIN
 * - FF_PROFILE_H264_HIGH
 * - ...
 *
 * For HEVC profile can typically be:
 * - FF_PROFILE_HEVC_MAIN
 * - FF_PROFILE_HEVC_MAIN_10 (10 bit channel precision)
 * - ...
 *
 * @see hvd_init
 */
struct hvd_config
{
	const char *hardware; //!< hardware type for decoding, e.g. "vaapi"
	const char *codec; //!< codec name, e.g. "h264", "vp8"
	const char *device; //!< NULL / "" or device, e.g. "/dev/dri/renderD128"
	const char *pixel_format; //!< NULL / "" for default or format, e.g. "rgb0", "bgr0", "nv12", "yuv420p"
	int width; //!< 0 to not specify, needed by some codecs
	int height; //!< 0 to not specify, needed by some codecs
	int profile; //!< 0 to leave as FF_PROFILE_UNKNOWN or profile e.g. FF_PROFILE_HEVC_MAIN, ...
};

/**
 * @struct hvd_packet
 * @brief Encoded data packet
 *
 * Fill data with pointer to your encoded data (no copying is needed).
 * Fill size with encoded data size in bytes
 *
 * WARNING The data member must be AV_INPUT_BUFFER_PADDING_SIZE
 * larger than the actual bytes because some optimized bitstream
 * readers read 32 or 64 bits at once and could read over the end.
 *
 * Pass hvd_packet with your data to hvd_send_packet.
 *
 * @see hvd_send_packet
 */
struct hvd_packet
{
	uint8_t *data; //!< pointer to encoded data
	int size; //!< size of encoded data
};

/**
  * @brief Constants returned by most of library functions
  */
enum hvd_retval_enum
{
	HVD_AGAIN=AVERROR(EAGAIN), //!< hvd_send_packet was not accepted (e.g. buffers full), use hvd_receive_frame before next call
	HVD_ERROR=-1, //!< error occured
	HVD_OK=0, //!< succesfull execution
};

/**
 * @brief Initialize internal library data.
 * @param config decoder configuration
 * @return
 * - pointer to internal library data
 * - NULL on error, errors printed to stderr
 *
 * @see hvd_config, hvd_close
 */
struct hvd *hvd_init(const struct hvd_config *config);

/**
 * @brief Free library resources
 *
 * Cleans and frees library memory.
 *
 * @param h pointer to internal library data
 * @see hvd_init
 *
 */
void hvd_close(struct hvd *h);

/**
 * @brief Send packet to hardware for decoding.
 *
 * Pass data in hvd_packet for decoding.
 * Follow with hvd_receive_frame to get decoded data from hardware.
 *
 * If you have simple loop like:
 * - send encoded data with hve_send_packet
 * - receive decoded data with hve_receive_frame
 *
 * HVD_AGAIN return value will never happen.
 * If you are sending lots of data and not reading you may get HVD_AGAIN when buffers are full.
 *
 * When you are done with decoding call with:
 * - NULL packet argument
 * - or packet with NULL data and 0 size
 * to flush the decoder and follow with hvd_receive_frame as usual to get last frames from decoder.
 *
 * After flushing and reading last frames you can follow with decoding new stream.
 *
 * Perfomance hints:
 *  - don't copy data from your source, pass the pointer in packet->data
 *
 * @param h pointer to internal library data
 * @param packet data to decode
 * @return
 * - HVD_OK on success
 * - HVD_ERROR on error
 * - HVD_AGAIN input was rejected, read data with hvd_receive_packet before next try
 *
 * @see hvd_packet, hvd_receive_frame
 *
 * Example flushing:
 * @code
 *  //flush
 *  hvd_send_packet(hardware_decoder, NULL);
 *
 *  //retrieve last frames from decoder as usual
 *	while( (frame=hvd_receive_frame(hardware_decoder, &failed)) )
 *	{
 *		//do something with frame->data, frame->linesize
 *	}
 *
 *	//NULL frame and non-zero failed indicates failure during decoding
 *	if(failed)
 *		//your logic on failure
 *
 *  //terminate or continue decoding new stream
 * @endcode
 *
 */
int hvd_send_packet(struct hvd *h, struct hvd_packet *packet);


/**
 * @brief Retrieve decoded frame data from hardware.
 *
 * Keep calling this functions after hvd_send_packet until NULL is returned.
 * The ownership of returned FFmpeg AVFrame remains with the library:
 * - consume it immidiately
 * - or copy the data
 *
 * @param h pointer to internal library data
 * @param error pointer to error code
 * @return
 * - AVFrame* pointer to FFMpeg AVFrame, you are mainly interested in data and linesize arrays
 * - NULL when no more data is pending, query error argument to check result (HVD_OK on success)
 *
 * @see hvd_send_packet
 *
 * Example (in decoding loop):
 * @code
 * //send data for hardware decoding
 * if(hvd_send_packet(h, packet) != HVD_OK)
 *   break; //break on error
 *
 * //get the decoded data
 * while( (frame = hvd_receive_frame(av, &error) ) )
 * {
 *   //do something with frame->data, frame->linesize
 * }
 *
 * //NULL frame and non-zero failed indicates failure during decoding
 * if(failed)
 *   break; //break on error
 *
 * @endcode
 *
 */
AVFrame *hvd_receive_frame(struct hvd *h, int *error);

/** @}*/

#ifdef __cplusplus
}
#endif

#endif
