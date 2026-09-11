#include "stdafx.h"
#include "pbml_observe.h"
#include "pbml_world.h"
#include "playerbot_empire_rules.h"

#include "char.h"
#include "item.h"
#include "sectree.h"

namespace
{
	const DWORD PBML_RED_POTIONS[] = { 27051, 27001, 27002, 27003 };

	class FCountHuntMobs
	{
		public:
			FCountHuntMobs(LPCHARACTER owner, int maxDistance) :
				m_owner(owner), m_maxDistance(maxDistance), m_count(0)
			{
			}

			bool operator () (LPENTITY entity)
			{
				if (!m_owner || !entity || !entity->IsType(ENTITY_CHARACTER))
					return true;
				LPCHARACTER candidate = (LPCHARACTER)entity;
				if (!candidate || candidate == m_owner)
					return true;
				if (candidate->IsDead() || !candidate->IsMonster())
					return true;
				if (candidate->GetMapIndex() != m_owner->GetMapIndex())
					return true;
				const int distance = DISTANCE_APPROX(
						m_owner->GetX() - candidate->GetX(),
						m_owner->GetY() - candidate->GetY());
				if (distance <= m_maxDistance)
					++m_count;
				return true;
			}

			int Count() const { return m_count; }

		private:
			LPCHARACTER m_owner;
			int m_maxDistance;
			int m_count;
	};

	int CountPotions(LPCHARACTER ch)
	{
		if (!ch || !ch->IsItemLoaded())
			return 0;
		int count = 0;
		for (WORD cell = 0; cell < INVENTORY_MAX_NUM; ++cell)
		{
			LPITEM item = ch->GetInventoryItem(cell);
			if (!item)
				continue;
			const DWORD vnum = item->GetVnum();
			for (size_t i = 0; i < sizeof(PBML_RED_POTIONS) / sizeof(PBML_RED_POTIONS[0]); ++i)
			{
				if (vnum == PBML_RED_POTIONS[i])
				{
					count += item->GetCount();
					break;
				}
			}
		}
		return count;
	}
}

void PbmlFillObservation(LPCHARACTER ch, LPCHARACTER target, float* out)
{
	if (!out)
		return;
	for (int i = 0; i < PBML_OBS_DIM; ++i)
		out[i] = 0.0f;
	if (!ch)
		return;

	out[PBML_OBS_HP] = ch->GetMaxHP() > 0
			? PbmlClamp01((float)ch->GetHP() / (float)ch->GetMaxHP()) : 0.0f;
	out[PBML_OBS_SP] = ch->GetMaxSP() > 0
			? PbmlClamp01((float)ch->GetSP() / (float)ch->GetMaxSP()) : 0.0f;
	out[PBML_OBS_LEVEL] = PbmlClamp01((float)ch->GetLevel() / (float)PBML_LEVEL_SCALE);

	long townX = 0, townY = 0, huntX = 0, huntY = 0;
	PbmlGetTownXY(townX, townY);
	PbmlGetHuntXY(huntX, huntY);
	const int townDist = DISTANCE_APPROX(ch->GetX() - townX, ch->GetY() - townY);
	out[PBML_OBS_IN_TOWN] = townDist <= PBML_TOWN_RADIUS ? 1.0f : 0.0f;
	out[PBML_OBS_DIST_HUNT] = PbmlDistObs(
			DISTANCE_APPROX(ch->GetX() - huntX, ch->GetY() - huntY));

	if (target && !target->IsDead() && target->GetMapIndex() == ch->GetMapIndex())
	{
		out[PBML_OBS_HAS_TARGET] = 1.0f;
		out[PBML_OBS_DIST_TARGET] = PbmlDistObs(
				DISTANCE_APPROX(ch->GetX() - target->GetX(), ch->GetY() - target->GetY()));
		out[PBML_OBS_TARGET_HP] = target->GetMaxHP() > 0
				? PbmlClamp01((float)target->GetHP() / (float)target->GetMaxHP()) : 0.0f;
		out[PBML_OBS_TARGET_LEVEL_DELTA] = PbmlClamp11(
				(float)((int)target->GetLevel() - (int)ch->GetLevel()) / 20.0f);
	}

	int nearby = 0;
	if (ch->GetSectree())
	{
		FCountHuntMobs counter(ch, PBML_SCAN_RANGE);
		ch->GetSectree()->ForEachAround(counter);
		nearby = counter.Count();
	}
	out[PBML_OBS_NEARBY_MOBS] = PbmlClamp01((float)nearby / 8.0f);

	LPITEM weapon = ch->GetWear(WEAR_WEAPON);
	out[PBML_OBS_HAS_WEAPON] = (weapon && weapon->GetType() == ITEM_WEAPON) ? 1.0f : 0.0f;
	out[PBML_OBS_POTIONS] = PbmlClamp01((float)CountPotions(ch) / 20.0f);
	out[PBML_OBS_MOVING] = ch->IsStateMove() ? 1.0f : 0.0f;

	const int role = playerbot_empire_rules::GetMapRole(ch->GetMapIndex());
	if (role == playerbot_empire_rules::MAP_ROLE_M1)
		out[PBML_OBS_MAP_M1] = 1.0f;
	else if (role == playerbot_empire_rules::MAP_ROLE_M2)
		out[PBML_OBS_MAP_M2] = 1.0f;
	else if (role == playerbot_empire_rules::MAP_ROLE_M3)
		out[PBML_OBS_MAP_M3] = 1.0f;
	else
		out[PBML_OBS_MAP_SHARED] = 1.0f;

	const int job = ch->GetJob() % 4;
	if (job == JOB_WARRIOR)
		out[PBML_OBS_JOB_WARRIOR] = 1.0f;
	else if (job == JOB_ASSASSIN)
		out[PBML_OBS_JOB_NINJA] = 1.0f;
	else if (job == JOB_SURA)
		out[PBML_OBS_JOB_SURA] = 1.0f;
	else
		out[PBML_OBS_JOB_SHAMAN] = 1.0f;
}
