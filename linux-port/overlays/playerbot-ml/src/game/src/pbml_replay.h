#ifndef __INC_METIN2_PBML_REPLAY_H__
#define __INC_METIN2_PBML_REPLAY_H__

#include "pbml_types.h"

void PbmlReplayAppend(const float* obs, EPbmlAction action, float reward, bool done);

#endif
