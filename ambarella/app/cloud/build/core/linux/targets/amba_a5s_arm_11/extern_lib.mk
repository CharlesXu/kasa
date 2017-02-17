##
## $(MODULE_NAME_TAG)/build/core/linux/extern_lib.mk
##
## History:
##    2013/03/16 - [Zhi He] Create
##
## Copyright (C) 2014 - 2024, the Ambarella, Inc.
##
## All rights reserved. No Part of this file may be reproduced, stored
## in a retrieval system, or transmitted, in any form, or by any means,
## electronic, mechanical, photocopying, recording, or otherwise,
## without the prior consent of the Ambarella, Inc.
##

EXTERN_LIB_FFMPEG_DIR = $(BUILDSYSTEM_DIR)/mw_ce/ffmpeg_v0.7
EXTERN_LIB_LIBXML2_DIR = $(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/libxml2/
EXTERN_LIB_LIBAAC_DIR = $(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/aac/
EXTERN_LIB_ALSA_DIR = $(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/alsa-lib/
EXTERN_LIB_PULSEAUDIO_DIR = $(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/pulseaudio/
EXTERN_BUILD_DIR = $(BUILDSYSTEM_DIR)/build/

EXTERN_LIB_FFMPEG_INC = -I$(EXTERN_LIB_FFMPEG_DIR)/libavutil -I$(EXTERN_LIB_FFMPEG_DIR) \
	-I$(EXTERN_LIB_FFMPEG_DIR)/libavcodec -I$(EXTERN_LIB_FFMPEG_DIR)/libavformat 
EXTERN_LIB_LIBXML2_INC = -I$(EXTERN_LIB_LIBXML2_DIR)/include/
EXTERN_LIB_LIBAAC_INC = -I$(EXTERN_LIB_LIBAAC_DIR)/include/ -I$(BUILDSYSTEM_DIR)/include/
EXTERN_LIB_ALSA_INC = -I$(EXTERN_LIB_ALSA_DIR)/include
EXTERN_LIB_PULSEAUDIO_INC = -I$(EXTERN_LIB_PULSEAUDIO_DIR)/include
EXTERN_BUILD_INC = -I$(EXTERN_BUILD_DIR)/include/
	
EXTERN_LIB_FFMPEG_LIB = -L$(EXTERN_LIB_FFMPEG_DIR)/build/linux
EXTERN_LIB_LIBXML2_LIB = -L$(EXTERN_LIB_LIBXML2_DIR)/lib/
EXTERN_LIB_LIBAAC_LIB = -L$(EXTERN_LIB_LIBAAC_DIR)/lib/
EXTERN_LIB_ALSA_LIB = -L$(EXTERN_LIB_ALSA_DIR)/usr/lib/
EXTERN_LIB_PULSEAUDIO_LIB = -L$(EXTERN_LIB_PULSEAUDIO_DIR)/usr/lib/

#third party lib for pulse audio
EXTERN_LIB_LIBFFI_LIB = -L$(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/libffi/usr/lib
EXTERN_LIB_EXPAT_LIB = -L$(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/expat/usr/lib
EXTERN_LIB_LIBSNDFILE_LIB = -L$(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/libsndfile/usr/lib
EXTERN_LIB_DBUS_LIB = -L$(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/dbus/usr/lib
EXTERN_LIB_LIBTOOL_LIB = -L$(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/libtool/usr/lib
EXTERN_LIB_LIBSAMPLERATE_LIB = -L$(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/libsamplerate/usr/lib
EXTERN_LIB_LIBSPEEX_LIB = -L$(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/speex/usr/lib
EXTERN_LIB_JSON-C_LIB = -L$(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/json-c/usr/lib
EXTERN_LIB_ORC_LIB = -L$(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/orc/usr/lib
EXTERN_LIB_GLIB2_LIB = -L$(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/glib2/usr/lib
EXTERN_LIB_ZLIB_LIB = -L$(BUILDSYSTEM_DIR)/prebuild/third-party/armv6k/zlib/usr/lib