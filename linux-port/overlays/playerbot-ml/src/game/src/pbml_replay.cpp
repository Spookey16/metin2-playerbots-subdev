#include "stdafx.h"
#include "pbml_replay.h"
#include "pbml_env.h"

#include "log.h"
#include "utils.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#ifdef _MSC_VER
typedef unsigned __int16 uint16_t;
typedef unsigned __int8 uint8_t;
#else
#include <stdint.h>
#endif

namespace
{
	const char kMagic[8] = { 'P', 'B', 'M', 'L', 'R', 'P', '0', '1' };
	const long kMaxBytes = 100L * 1024L * 1024L;

#pragma pack(push, 1)
	struct TPbmlReplayHeader
	{
		char magic[8];
		uint16_t obs_dim;
		uint16_t n_actions;
		uint16_t rec_size;
		uint16_t version;
	};

	struct TPbmlReplayRec
	{
		float obs[PBML_OBS_DIM];
		uint8_t action;
		uint8_t done;
		uint16_t pad;
		float reward;
	};
#pragma pack(pop)

	FILE* s_fp = NULL;
	bool s_full = false;
	DWORD s_nextSizeCheck = 0;
	bool s_loggedOpen = false;
	bool s_loggedFull = false;

	const char* ReplayPath()
	{
		return PbmlEnvStr("PLAYERBOT_ML_REPLAY_PATH", "pbml_replay.bin");
	}

	void CloseReplay()
	{
		if (s_fp)
		{
			fclose(s_fp);
			s_fp = NULL;
		}
	}

	bool EnsureReplay()
	{
		if (s_full)
			return false;
		if (s_fp)
			return true;

		const char* path = ReplayPath();
		struct stat st;
		const bool exists = stat(path, &st) == 0;
		if (exists && st.st_size >= kMaxBytes)
		{
			s_full = true;
			return false;
		}

		s_fp = fopen(path, exists ? "ab" : "wb");
		if (!s_fp)
			return false;
		if (!exists)
		{
			TPbmlReplayHeader header;
			memcpy(header.magic, kMagic, 8);
			header.obs_dim = (uint16_t)PBML_OBS_DIM;
			header.n_actions = (uint16_t)PBML_ACT_COUNT;
			header.rec_size = (uint16_t)sizeof(TPbmlReplayRec);
			header.version = 1;
			if (fwrite(&header, sizeof(header), 1, s_fp) != 1)
			{
				CloseReplay();
				return false;
			}
		}
		if (!s_loggedOpen)
		{
			sys_log(0, "PLAYERBOT_ML: recording replay %s rec=%u",
					path, (unsigned int)sizeof(TPbmlReplayRec));
			s_loggedOpen = true;
		}
		return true;
	}

	void MaybeCap(DWORD now)
	{
		if (!s_fp)
			return;
		if (s_nextSizeCheck != 0 && now < s_nextSizeCheck)
			return;
		s_nextSizeCheck = now + 5000;
		fflush(s_fp);
		const long pos = ftell(s_fp);
		if (pos >= kMaxBytes)
		{
			CloseReplay();
			s_full = true;
			if (!s_loggedFull)
			{
				sys_log(0, "PLAYERBOT_ML: replay file reached %ld bytes, stopping", kMaxBytes);
				s_loggedFull = true;
			}
		}
	}
}

void PbmlReplayAppend(const float* obs, EPbmlAction action, float reward, bool done)
{
	if (!PbmlEnvOn("PLAYERBOT_ML_REPLAY", false) || !obs)
		return;
	if (action < 0 || action >= PBML_ACT_COUNT)
		action = PBML_ACT_WAIT;
	if (!EnsureReplay())
		return;

	TPbmlReplayRec rec;
	memset(&rec, 0, sizeof(rec));
	memcpy(rec.obs, obs, sizeof(rec.obs));
	rec.action = (uint8_t)action;
	rec.done = done ? 1 : 0;
	rec.reward = reward;
	if (fwrite(&rec, sizeof(rec), 1, s_fp) != 1)
	{
		CloseReplay();
		return;
	}
	MaybeCap(get_dword_time());
}
