#ifndef __INC_METIN2_PBML_POLICY_H__
#define __INC_METIN2_PBML_POLICY_H__

#include "pbml_types.h"

// Scripted fallback: the same rules Tick used before a weight file existed.
EPbmlAction PbmlScriptedAction(const float* obs);

// MLP if pbml_policy.txt (or PLAYERBOT_ML_WEIGHTS) loaded; otherwise scripted.
// PLAYERBOT_ML_POLICY=scripted forces the fallback. auto is the default.
EPbmlAction PbmlChooseAction(const float* obs);
bool PbmlPolicyHasWeights();

#endif
