/*
 * customized_audio_decoder.cpp
 *
 * History:
 *    2014/02/20 - [Zhi He]  create file
 *
 * Copyright (C) 2014 - 2024, the Ambarella Inc.
 *
 * All rights reserved. No Part of this file may be reproduced, stored
 * in a retrieval system, or transmitted, in any form, or by any means,
 * electronic, mechanical, photocopying, recording, or otherwise,
 * without the prior consent of the Ambarella Inc.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>

#include "common_config.h"
#include "common_types.h"
#include "common_osal.h"
#include "common_utils.h"

#include "common_log.h"

#include "common_base.h"

#include "media_mw_if.h"
#include "media_mw_utils.h"

#include "framework_interface.h"
#include "mw_internal.h"
#include "dsp_platform_interface.h"
#include "streaming_if.h"
#include "modules_interface.h"

#include "customized_audio_encoder.h"

DCONFIG_COMPILE_OPTION_CPPFILE_IMPLEMENT_BEGIN
DCODE_DELIMITER;

IAudioEncoder *gfCreateCustomizedAudioEncoderModule(const TChar *pName, const volatile SPersistMediaConfig *pPersistMediaConfig, IMsgSink *pEngineMsgSink)
{
    return CCustomizedAudioEncoder::Create(pName, pPersistMediaConfig, pEngineMsgSink);
}

IAudioEncoder *CCustomizedAudioEncoder::Create(const TChar *pname, const volatile SPersistMediaConfig *pPersistMediaConfig, IMsgSink *pMsgSink)
{
    CCustomizedAudioEncoder *result = new CCustomizedAudioEncoder(pname, pPersistMediaConfig, pMsgSink);
    if (result && result->Construct() != EECode_OK) {
        delete result;
        result = NULL;
    }
    return result;
}

void CCustomizedAudioEncoder::Destroy()
{
    Delete();
}

void CCustomizedAudioEncoder::Delete()
{
    LOGM_RESOURCE("CCustomizedAudioEncoder::Delete().\n");
    if (mpCodec) {
        mpCodec->Destroy();
        mpCodec = NULL;
    }

    inherited::Delete();
}

CCustomizedAudioEncoder::CCustomizedAudioEncoder(const TChar *pname, const volatile SPersistMediaConfig *pPersistMediaConfig, IMsgSink *pMsgSink)
    : inherited(pname)
    , mpCodec(NULL)
    , mpPersistMediaConfig(pPersistMediaConfig)
{
}

EECode CCustomizedAudioEncoder::Construct()
{
    DSET_MODULE_LOG_CONFIG(ELogModuleCustomizedAudioDecoder);

    mpCodec = gfCustomizedCodecFactory((TChar *)"customized_adpcm", mIndex);
    if (DUnlikely(!mpCodec)) {
        LOG_ERROR("gfCreateCustomizedADPCMCodec return fail\n");
        return EECode_Error;
    }

    //mConfigLogLevel = ELogLevel_Debug;
    //mConfigLogOption = DCAL_BITMASK(ELogOption_Flow) | DCAL_BITMASK(ELogOption_State);

    return EECode_OK;
}

CCustomizedAudioEncoder::~CCustomizedAudioEncoder()
{
    if (mpCodec) {
        mpCodec->Destroy();
        mpCodec = NULL;
    }
}

EECode CCustomizedAudioEncoder::SetupContext(SAudioParams *param)
{
    switch (param->codec_format) {

        case StreamFormat_CustomizedADPCM_1:
            break;

        default:
            LOGM_ERROR("codec_format=%u not supported.\n", param->codec_format);
            return EECode_Error;
    }

    return EECode_OK;
}

void CCustomizedAudioEncoder::DestroyContext()
{
}

EECode CCustomizedAudioEncoder::Encode(TU8 *p_in, TU8 *&p_out, TU32 &output_size)
{
    EECode err = EECode_OK;

    mDebugHeartBeat ++;

    LOGM_FATAL("todo check: CCustomizedAudioEncoder::Encode\n");

    if (DUnlikely(!mpCodec)) {
        LOGM_FATAL("NULL mpCodec\n");
        return EECode_InternalLogicalBug;
    }

    TMemSize size = 0;
    err = mpCodec->Encoding((void *) p_in, (void *) p_out, 2048, size);
    DASSERT_OK(err);
    output_size = size;

    return err;
}

EECode CCustomizedAudioEncoder::GetExtraData(TU8 *&p, TU32 &size)
{
    LOGM_ERROR("CCustomizedAudioEncoder: no extra data\n");
    return EECode_NoImplement;
}

void CCustomizedAudioEncoder::PrintStatus()
{
    LOGM_PRINTF("heart beat %d\n", mDebugHeartBeat);

    mDebugHeartBeat = 0;
}

DCONFIG_COMPILE_OPTION_CPPFILE_IMPLEMENT_END
