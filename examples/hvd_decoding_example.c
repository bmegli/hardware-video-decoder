/*
 * HVD Hardware Video Decoder example (template for simple program)
 *
 * Copyright 2019-2023 (C) Bartosz Meglicki <meglickib@gmail.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 *
 * This example needs your source of data to be complete.
 */

#include "../hvd.h"

#include <stdio.h>

void decoding_loop(struct hvd *hardware_decoder);
int process_user_input(int argc, char **argv, struct hvd_config *config);
int hint_on_init_failure_and_return_1(const struct hvd_config *hardware_config);

int main(int argc, char **argv)
{
	struct hvd_config hardware_config = {0};
	struct hvd* hardware_decoder;

	if(process_user_input(argc, argv, &hardware_config) != 0)
		return 1;

	if( (hardware_decoder = hvd_init(&hardware_config) ) == NULL )
		return hint_on_init_failure_and_return_1(&hardware_config);

	printf("initialized decoder...\n");

	//...
	//whatever logic you have to prepare data source
	//...

	decoding_loop(hardware_decoder);

	//...
	//whatever logic you have to clean data source
	//...

	hvd_close(hardware_decoder);

	printf("closed decoder...\n");
	printf("bye...\n");

	return 0;
}

void decoding_loop(struct hvd *hardware_decoder)
{
	struct hvd_packet packet={0}; //here we will be passing encoded data
	AVFrame *frame; //FFmpeg AVFrame, here we will be getting decoded data
	int keep_decoding=0, error; //we have nothing to decode in example!

	printf("there is no data source in example...\n");
	printf("...so don't expect anything other than...\n");
	printf("...initialization and cleanup of hardware\n");

	while(keep_decoding)
	{
		//...
		//update your_data in some way (e.g. file read, network)
		//...

		//fill hvd_packet with encoded data

		//packet.data = your_data; //set pointer to your encoded data
		//packet.size = your_data_size; //here some dummy size for dummy data

		if( hvd_send_packet(hardware_decoder, &packet) != HVD_OK )
		{
			fprintf(stderr, "failed to send data for decoding");
			break;
		}

		while( (frame = hvd_receive_frame(hardware_decoder, &error) ) != NULL)
		{
			//...
			//consume decoded video data in the frame (e.g. use frame.data, frame.linesize)
			//...
		}

		if(error != HVD_OK)
		{
			fprintf(stderr, "failed to decode data\n");
			break; //something bad happened
		}
	}

	printf("flushing decoder...\n");
	hvd_send_packet(hardware_decoder, NULL);
	while( (frame = hvd_receive_frame(hardware_decoder, &error) ) != NULL)
		; //do whatever you want with some last frames, here ignoring
}

int process_user_input(int argc, char **argv, struct hvd_config *config)
{
	if(argc < 3)
	{
		fprintf(stderr, "Usage: %s <hardware> <codec> [device] [width] [height] [profile]\n\n", argv[0]);
		fprintf(stderr, "examples: \n");
		fprintf(stderr, "%s vaapi h264 \n", argv[0]);
		fprintf(stderr, "%s vdpau h264 \n", argv[0]);
		fprintf(stderr, "%s vaapi h264 /dev/dri/renderD128\n", argv[0]);
		fprintf(stderr, "%s vaapi h264 /dev/dri/renderD129\n", argv[0]);
		fprintf(stderr, "%s dxva2 h264 \n", argv[0]);
		fprintf(stderr, "%s d3d11va h264 \n", argv[0]);
		fprintf(stderr, "%s videotoolbox h264 \n", argv[0]);
		fprintf(stderr, "%s cuda h264_cuvid \n", argv[0]);
		fprintf(stderr, "%s cuda hevc_cuvid \n", argv[0]);
		fprintf(stderr, "%s vaapi hevc /dev/dri/renderD128 848 480 1 \n", argv[0]);
		return 1;
	}

	config->hardware = argv[1];
	config->codec = argv[2];
	config->device = argv[3]; //NULL or device, both are ok

	if(argc >= 5) config->width = atoi(argv[4]);
	if(argc >= 6) config->height = atoi(argv[5]);
	if(argc >= 7) config->profile = atoi(argv[6]);

	return 0;
}

int hint_on_init_failure_and_return_1(const struct hvd_config *hardware_config)
{
	fprintf(stderr, "failed to initalize hardware decoder for %s\n", hardware_config->hardware);
	fprintf(stderr, "hints:\n");
	fprintf(stderr, "- try using other device? (not %s)\n", hardware_config->device ? hardware_config->device : "NULL");
	fprintf(stderr, "- try using other hardware? (not %s)\n", hardware_config->hardware);
	return 1;
}
