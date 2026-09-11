#include "stdafx.h"
#include "pbml_policy.h"
#include "pbml_env.h"

#include "utils.h"
#include "log.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace
{
	struct TPbmlMlp
	{
		int obs;
		int hidden;
		int actions;
		std::vector<float> w1;
		std::vector<float> b1;
		std::vector<float> w2;
		std::vector<float> b2;
		bool loaded;
		time_t mtime;
		std::string path;
		DWORD nextCheck;
		bool loggedMissing;
		bool loggedLoaded;

		TPbmlMlp() :
			obs(0), hidden(0), actions(0), loaded(false), mtime(0),
			nextCheck(0), loggedMissing(false), loggedLoaded(false)
		{
		}
	};

	TPbmlMlp s_mlp;

	const char* WeightsPath()
	{
		return PbmlEnvStr("PLAYERBOT_ML_WEIGHTS", "pbml_policy.txt");
	}

	bool ReadFloats(FILE* fp, std::vector<float>& out, size_t n)
	{
		out.assign(n, 0.0f);
		for (size_t i = 0; i < n; ++i)
		{
			if (fscanf(fp, "%f", &out[i]) != 1)
				return false;
		}
		return true;
	}

	bool LoadFile(const char* path, time_t mtime)
	{
		FILE* fp = fopen(path, "r");
		if (!fp)
			return false;

		char magic[16];
		int version = 0;
		int obs = 0;
		int hidden = 0;
		int actions = 0;
		if (fscanf(fp, "%15s %d", magic, &version) != 2 ||
				strcmp(magic, "PBMLMLP") != 0 || version != 1 ||
				fscanf(fp, "%d %d %d", &obs, &hidden, &actions) != 3)
		{
			fclose(fp);
			return false;
		}
		if (obs != PBML_OBS_DIM || actions != PBML_ACT_COUNT || hidden <= 0 || hidden > 256)
		{
			fclose(fp);
			return false;
		}

		TPbmlMlp next;
		next.obs = obs;
		next.hidden = hidden;
		next.actions = actions;
		if (!ReadFloats(fp, next.w1, (size_t)hidden * (size_t)obs) ||
				!ReadFloats(fp, next.b1, (size_t)hidden) ||
				!ReadFloats(fp, next.w2, (size_t)actions * (size_t)hidden) ||
				!ReadFloats(fp, next.b2, (size_t)actions))
		{
			fclose(fp);
			return false;
		}
		fclose(fp);

		next.loaded = true;
		next.mtime = mtime;
		next.path = path;
		next.loggedLoaded = false;
		s_mlp = next;
		sys_log(0, "PLAYERBOT_ML: loaded policy %s hidden=%d obs=%d actions=%d",
				path, hidden, obs, actions);
		s_mlp.loggedLoaded = true;
		return true;
	}

	void MaybeReload(DWORD now)
	{
		if (s_mlp.nextCheck != 0 && now < s_mlp.nextCheck)
			return;
		s_mlp.nextCheck = now + 5000;

		const char* path = WeightsPath();
		struct stat st;
		if (stat(path, &st) != 0)
		{
			s_mlp.loaded = false;
			if (!s_mlp.loggedMissing)
			{
				sys_log(0, "PLAYERBOT_ML: no weight file at %s (scripted policy)", path);
				s_mlp.loggedMissing = true;
			}
			return;
		}
		s_mlp.loggedMissing = false;
		if (s_mlp.loaded && s_mlp.mtime == st.st_mtime && s_mlp.path == path)
			return;
		if (!LoadFile(path, st.st_mtime) && !s_mlp.loggedMissing)
		{
			sys_err("PLAYERBOT_ML: failed to parse %s (expected PBMLMLP 1 / obs hidden actions)", path);
			s_mlp.loggedMissing = true;
		}
	}

	int Argmax(const std::vector<float>& y)
	{
		int best = 0;
		for (size_t i = 1; i < y.size(); ++i)
		{
			if (y[i] > y[best])
				best = (int)i;
		}
		return best;
	}

	void Forward(const float* obs, std::vector<float>& y)
	{
		const int hidden = s_mlp.hidden;
		const int obsN = s_mlp.obs;
		const int actN = s_mlp.actions;
		std::vector<float> h((size_t)hidden);
		for (int i = 0; i < hidden; ++i)
		{
			float s = s_mlp.b1[(size_t)i];
			const float* row = &s_mlp.w1[(size_t)i * (size_t)obsN];
			for (int j = 0; j < obsN; ++j)
				s += row[j] * obs[j];
			h[(size_t)i] = s > 0.0f ? s : 0.0f;
		}
		y.assign((size_t)actN, 0.0f);
		for (int i = 0; i < actN; ++i)
		{
			float s = s_mlp.b2[(size_t)i];
			const float* row = &s_mlp.w2[(size_t)i * (size_t)hidden];
			for (int j = 0; j < hidden; ++j)
				s += row[j] * h[(size_t)j];
			y[(size_t)i] = s;
		}
	}
}

EPbmlAction PbmlScriptedAction(const float* obs)
{
	if (!obs)
		return PBML_ACT_WAIT;
	if (obs[PBML_OBS_IN_TOWN] > 0.5f)
		return PBML_ACT_GO_HUNT;
	if (obs[PBML_OBS_HAS_TARGET] > 0.5f)
	{
		const float melee = (float)PBML_MELEE_RANGE / PBML_DIST_SCALE;
		if (obs[PBML_OBS_DIST_TARGET] > melee)
			return PBML_ACT_APPROACH;
		return PBML_ACT_ATTACK;
	}
	return PBML_ACT_WANDER;
}

EPbmlAction PbmlChooseAction(const float* obs)
{
	const EPbmlAction scripted = PbmlScriptedAction(obs);
	const char* mode = PbmlEnvStr("PLAYERBOT_ML_POLICY", "auto");
	if (mode[0] == 's')
		return scripted;

	MaybeReload(get_dword_time());
	if (!s_mlp.loaded)
		return scripted;

	std::vector<float> logits;
	Forward(obs, logits);
	int action = Argmax(logits);

	const int epsPermille = PbmlEnvInt("PLAYERBOT_ML_EPSILON_PERMILLE", 0, 0, 1000);
	if (epsPermille > 0 && number(1, 1000) <= epsPermille)
		action = number(0, PBML_ACT_COUNT - 1);

	if (action < 0 || action >= PBML_ACT_COUNT)
		return scripted;
	return (EPbmlAction)action;
}

bool PbmlPolicyHasWeights()
{
	MaybeReload(get_dword_time());
	return s_mlp.loaded;
}
