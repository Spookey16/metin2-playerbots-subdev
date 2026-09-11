#ifndef __INC_METIN2_PBML_TYPES_H__
#define __INC_METIN2_PBML_TYPES_H__

// Observation / action contract for the Shinsoo ML module.
// Keep this file free of engine types so the Python trainer can mirror it.
//
// Positions are relative (distance to hunt camp / target), map identity is a
// role (M1/M2/M3/shared), never a Shinsoo-only map number. That is what lets
// the same weights transfer if Jinno is wired the same way later.

enum EPbmlObs
{
	PBML_OBS_HP = 0,
	PBML_OBS_SP,
	PBML_OBS_LEVEL,
	PBML_OBS_IN_TOWN,
	PBML_OBS_DIST_HUNT,
	PBML_OBS_HAS_TARGET,
	PBML_OBS_DIST_TARGET,
	PBML_OBS_TARGET_HP,
	PBML_OBS_TARGET_LEVEL_DELTA,
	PBML_OBS_NEARBY_MOBS,
	PBML_OBS_HAS_WEAPON,
	PBML_OBS_POTIONS,
	PBML_OBS_MOVING,
	PBML_OBS_MAP_M1,
	PBML_OBS_MAP_M2,
	PBML_OBS_MAP_M3,
	PBML_OBS_MAP_SHARED,
	PBML_OBS_JOB_WARRIOR,
	PBML_OBS_JOB_NINJA,
	PBML_OBS_JOB_SURA,
	PBML_OBS_JOB_SHAMAN,
	PBML_OBS_DIM // 21; keep in sync with train_policy.py OBS_DIM
};

enum EPbmlAction
{
	PBML_ACT_WAIT = 0,
	PBML_ACT_GO_HUNT,
	PBML_ACT_APPROACH,
	PBML_ACT_ATTACK,
	PBML_ACT_WANDER,
	PBML_ACT_COUNT
};

// Fallbacks. Live town is GetTownServices(Shinsoo M1).blacksmith from the
// 1.32 catalog (Yongan). Hunt is PLAYERBOT_GROUND_HUBS_1[11]: a level-3
// spawn outside the safe zone, measured from the same map files.
const long PBML_TOWN_X = 476800;
const long PBML_TOWN_Y = 951600;
const long PBML_HUNT_X = 464000;
const long PBML_HUNT_Y = 937200;
const int PBML_TOWN_RADIUS = 4000;
const int PBML_SCAN_RANGE = 6000;
const int PBML_MELEE_RANGE = 280;
const int PBML_HUNT_ARRIVE = 600;
const float PBML_DIST_SCALE = 8000.0f;
const int PBML_LEVEL_SCALE = 120;
const int PBML_DEFAULT_HIDDEN = 32;

inline float PbmlClamp01(float x)
{
	if (x < 0.0f)
		return 0.0f;
	if (x > 1.0f)
		return 1.0f;
	return x;
}

inline float PbmlClamp11(float x)
{
	if (x < -1.0f)
		return -1.0f;
	if (x > 1.0f)
		return 1.0f;
	return x;
}

inline float PbmlDistObs(int distance)
{
	if (distance <= 0)
		return 0.0f;
	return PbmlClamp01((float)distance / PBML_DIST_SCALE);
}

#endif
