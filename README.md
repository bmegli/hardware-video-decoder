# HVD - Hardware Video Decoder C library

This library wraps hardware video decoding in a simple interface.
There are no performance loses (at the cost of library flexibility).

Currently it supports VAAPI, VDPAU, NVDEC/CUVID (tested) and a few other hardware decoders (not tested).\
Various codecs are supported (e.g. H.264, HEVC, VP8, VP9)

See library [documentation](https://bmegli.github.io/hardware-video-decoder/group__interface.html).

See also twin [HVE](https://github.com/bmegli/hardware-video-encoder) Hardware Video Encoder library.

See [hardware-video-streaming](https://github.com/bmegli/hardware-video-streaming) for other related projects.

## Intended Use

Raw H.264 (and other codecs) decoding:
- custom network streaming protocols
- low latency streaming
- ...

Complex pipelines (demuxing, scaling, color conversions, filtering) are beyond the scope of this library.

## Platforms 

Cross-platform but tested only on Linux (Ubuntu 18.04).

## Hardware

Tested with:
- Intel VAAPI compatible hardware decoders ([Quick Sync Video](https://ark.intel.com/Search/FeatureFilter?productType=processors&QuickSyncVideo=true))
- AMD/ATI VAAPI compatible hardware decoders
- VDPAU compatible hardware decoders (e.g. Nvidia GPU)
- Nvidia NVDEC compatible hardware decoders (e.g. Nvidia GPU)

Also implemented (but not tested):
- DirectX 9 Video Acceleration (dxva2)
- DirectX 11 Video Acceleration (d3d11va)
- VideoToolbox

## Dependencies

Library depends on:
- FFmpeg `avcodec` and `avutil` (at least 3.4 version)

Works with:
- system FFmpeg on Ubuntu 18.04
- system FFmpeg on Ubuntu 20.04

## Building Instructions

Tested on Ubuntu 18.04 and 20.04

``` bash
# update package repositories
sudo apt-get update 
# get avcodec and avutil (and ffmpeg for testing)
sudo apt-get install ffmpeg libavcodec-dev libavutil-dev
# get compilers and make and cmake
sudo apt-get install build-essential
# get cmake - we need to specify libcurl4 for Ubuntu 18.04 dependencies problem
sudo apt-get install libcurl4 cmake
# get git
sudo apt-get install git
# clone the repository
git clone https://github.com/bmegli/hardware-video-decoder.git

# finally build the library and examples
cd hardware-video-decoder
mkdir build
cd build
cmake ..
make
```

## Running Example

```bash
./hvd-decoding-example
```

Follow with printed usage examples.

## Using

See examples directory for a more complete and commented examples with error handling.

There are just 4 functions and 3 user-visible data types:
- `hvd_init`
- `hvd_send_packet` (sends compressed data to hardware)
- `hvd_receive_frame` (retrieves uncompressed data from hardware)
- `hvd_close`

```C
	struct hvd_config hardware_config = {"vaapi", "h264", "/dev/dri/renderD128", "rgb0"};
	struct hvd *hardware_decoder = hvd_init(&hardware_config);
	struct hvd_packet packet= {0}; //here we will be passing encoded data

	//...
	//whatever logic you have to prepare data source
	//...

	while(keep_decoding)
	{
		//...
		//update your_data in some way (e.g. file read, network)
		//...
		packet.data = your_data; //set pointer to your encoded data
		packet.size = your_data_size; //here some dummy size for dummy data

		hvd_send_packet(hardware_decoder, &packet);

		AVFrame *frame; //FFmpeg AVFrame, here we will be getting decoded data
		int error;

		while( (frame = hvd_receive_frame(hardware_decoder, &error) ) != NULL)
		{
			//...
			//consume decoded video data in the frame (e.g. use frame.data, frame.linesize)
			//...
		}
	}
	//flush the decoder when your are done by sending NULL packet
	hvd_send_packet(hardware_decoder, NULL);
	while( (frame = hvd_receive_frame(hardware_decoder, &error) ) != NULL)
		; //do whatever you want with some last frames, here ignoring

	hvd_close(hardware_decoder);
```

That's it! You have just seen all the functions and data types in the library.

## Compiling your code

You have several options.

### IDE (recommended)

For static linking of HVD and dynamic linking of FFmpeg libraries (easiest):
- copy `hvd.h` and `hvd.c` to your project and add them in your favourite IDE
- add `avcodec` and `avutil` to linked libraries in IDE project configuration

For dynamic linking of HVD and FFmpeg libraries:
- place `hvd.h` where compiler can find it (e.g. `make install` for `/usr/local/include/hvd.h`)
- place `libhvd.so` where linker can find it (e.g. `make install` for `/usr/local/lib/libhvd.so`)
- make sure `/usr/local/...` is considered for libraries
- add `hvd`, `avcodec` and `avutil` to linked libraries in IDE project configuration
- make sure `libhvd.so` is reachable to you program at runtime (e.g. set `LD_LIBRARIES_PATH`)

### CMake

Assuming directory structure with HVD as `hardware-video-decoder` subdirectory (or git submodule) of your project.

```
your-project
│   main.cpp
│   CMakeLists.txt
│
└───hardware-video-decoder
│   │   hvd.h
│   │   hvd.c
│   │   CMakeLists.txt
```

You may use the following top level CMakeLists.txt

``` CMake
cmake_minimum_required(VERSION 3.0)

project(
    your-project
)

# drop the SHARED if you would rather link with HVE statically
add_library(hvd SHARED hardware-video-decoder/hvd.c)

add_executable(your-project main.cpp)
target_include_directories(your-project PRIVATE hardware-video-decoder)
target_link_libraries(your-project hvd avcodec avutil)
```

### Manually

Assuming your `main.c`/`main.cpp` and `hvd.h`, `hvd.c` are all in the same directory:

C
```bash
gcc main.c hvd.c -lavcodec -lavutil -o your-program
```

C++
```bash
gcc -c hvd.c
g++ -c main.cpp
g++ hvd.o main.o -lavcodec -lavutil -o your program
```

## License

Library is licensed under Mozilla Public License, v. 2.0

This is similiar to LGPL but more permissive:
- you can use it as LGPL in prioprietrary software
- unlike LGPL you may compile it statically with your code

Like in LGPL, if you modify this library, you have to make your changes available.
Making a github fork of the library with your changes satisfies those requirements perfectly.

You are linking to FFmpeg libraries. Consider also `avcodec` and `avutil` and the codec licensing.
