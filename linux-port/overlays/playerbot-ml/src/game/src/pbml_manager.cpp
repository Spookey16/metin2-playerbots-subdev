#include "stdafx.h"
#include "pbml_manager.h"
#include "pbml_env.h"
#include "pbml_observe.h"
#include "pbml_policy.h"
#include "pbml_replay.h"
#include "pbml_types.h"
#include "pbml_world.h"

#include "playerbot_empire_rules.h"
#include "playerbot_manager.h"

#include "battle.h"
#include "char.h"
#include "char_manager.h"
#include "cmd.h"
#include "config.h"
#include "constants.h"
#include "desc.h"
#include "item.h"
#include "log.h"
#include "motion.h"
#include "packet.h"
#include "sectree.h"
#include "utils.h"
#include "buffer_manager.h"

#include <cstring>
#include <map>

namespace
{
	const DWORD PBML_REVIVE_DELAY_MS = 4000;
	const DWORD PBML_ATTACK_INTERVAL_MS = 480;
	const DWORD PBML_POTION_INTERVAL_MS = 1500;
	const int PBML_DEFAULT_SPAWN = 24;
	const int PBML_MAX_SPAWN = 200;
	const float PBML_EXP_REWARD_SCALE = 400.0f;

	const DWORD PBML_RED_POTIONS[] = { 27051, 27001, 27002, 27003 };

	struct TPbmlState
	{
		TPbmlState() :
			dwNextAttackTime(0),
			dwDeathDetectedTime(0),
			dwNextReviveAttemptTime(0),
			dwNextPotionTime(0),
			dwNextWanderTime(0),
			dwTargetVID(0),
			bComboMotion(MOTION_COMBO_ATTACK_1),
			lLastX(0),
			lLastY(0),
			dwStuckSince(0),
			dwLastExp(0),
			bLastLevel(0),
			lastAction(PBML_ACT_WAIT),
			hasLastObs(false)
		{
			memset(lastObs, 0, sizeof(lastObs));
		}

		DWORD dwNextAttackTime;
		DWORD dwDeathDetectedTime;
		DWORD dwNextReviveAttemptTime;
		DWORD dwNextPotionTime;
		DWORD dwNextWanderTime;
		DWORD dwTargetVID;
		BYTE bComboMotion;
		long lLastX;
		long lLastY;
		DWORD dwStuckSince;
		DWORD dwLastExp;
		BYTE bLastLevel;
		float lastObs[PBML_OBS_DIM];
		EPbmlAction lastAction;
		bool hasLastObs;
	};

	typedef std::map<DWORD, TPbmlState> TPbmlStateMap;
	TPbmlStateMap s_mapState;

	class FFindNearestHunt
	{
		public:
			FFindNearestHunt(LPCHARACTER owner, int maxDistance) :
				m_owner(owner),
				m_maxDistance(maxDistance),
				m_best(NULL),
				m_bestDist(maxDistance + 1)
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
				if (candidate->GetLevel() > m_owner->GetLevel() + 8)
					return true;
				const int distance = DISTANCE_APPROX(
						m_owner->GetX() - candidate->GetX(),
						m_owner->GetY() - candidate->GetY());
				if (distance > m_maxDistance || distance >= m_bestDist)
					return true;
				m_bestDist = distance;
				m_best = candidate;
				return true;
			}

			LPCHARACTER Best() const { return m_best; }

		private:
			LPCHARACTER m_owner;
			int m_maxDistance;
			LPCHARACTER m_best;
			int m_bestDist;
	};

	LPCHARACTER FindNearestHunt(LPCHARACTER ch)
	{
		if (!ch || !ch->GetSectree())
			return NULL;
		FFindNearestHunt finder(ch, PBML_SCAN_RANGE);
		ch->GetSectree()->ForEachAround(finder);
		return finder.Best();
	}

	void SendSwingPacket(LPCHARACTER ch, LPCHARACTER target, BYTE comboMotion)
	{
		if (!ch || !ch->GetSectree())
			return;

		ch->OnMove(true);
		ch->ResetStopTime();

		if (comboMotion < MOTION_COMBO_ATTACK_1 || comboMotion > MOTION_COMBO_ATTACK_4)
			comboMotion = MOTION_COMBO_ATTACK_1;

		TPacketGCMove pack;
		pack.bHeader = HEADER_GC_MOVE;
		pack.bFunc = FUNC_COMBO;
		pack.bArg = comboMotion;
		pack.bRot = (BYTE)(ch->GetRotation() / 5);
		pack.dwVID = ch->GetVID();
		pack.lX = ch->GetX();
		pack.lY = ch->GetY();
		pack.dwTime = get_dword_time();
		pack.dwDuration = 0;
		ch->PacketAround(&pack, sizeof(TPacketGCMove));
		(void)target;
	}

	void Swing(LPCHARACTER ch, LPCHARACTER target, TPbmlState& state, DWORD dwNow)
	{
		if (!ch || !target || ch->IsDead() || target->IsDead())
			return;
		if (ch->GetMapIndex() != target->GetMapIndex())
			return;
		if (ch->IsStateMove() || dwNow < state.dwNextAttackTime)
			return;
		if (DISTANCE_APPROX(ch->GetX() - target->GetX(), ch->GetY() - target->GetY()) > PBML_MELEE_RANGE)
			return;

		ch->SetPosition(POS_FIGHTING);
		ch->SetVictim(target);
		ch->SetRotationToXY(target->GetX(), target->GetY());
		state.dwNextAttackTime = dwNow + PBML_ATTACK_INTERVAL_MS;
		SendSwingPacket(ch, target, state.bComboMotion);

		int damage = 0;
		LPITEM weapon = ch->GetWear(WEAR_WEAPON);
		if (weapon && weapon->GetType() == ITEM_WEAPON && weapon->GetSubType() != WEAPON_BOW)
			damage = CalcMeleeDamage(ch, target, false, false);
		if (damage < 5)
			damage = number(15, 35) + ch->GetLevel() * 4;
		target->Damage(ch, damage, DAMAGE_TYPE_NORMAL);
		target->SetSyncOwner(ch);
		if (!target->IsDead() && target->CanBeginFight())
			target->BeginFight(ch);

		++state.bComboMotion;
		if (state.bComboMotion > MOTION_COMBO_ATTACK_4)
			state.bComboMotion = MOTION_COMBO_ATTACK_1;
	}

	bool DrinkPotion(LPCHARACTER ch, TPbmlState& state, DWORD dwNow)
	{
		if (!ch || !ch->IsItemLoaded() || dwNow < state.dwNextPotionTime)
			return false;
		if (ch->GetMaxHP() <= 0 || ch->GetHP() * 100 >= ch->GetMaxHP() * 40)
			return false;

		state.dwNextPotionTime = dwNow + PBML_POTION_INTERVAL_MS;
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
					ch->UseItem(TItemPos(INVENTORY, cell));
					return true;
				}
			}
		}
		return false;
	}

	void WalkTo(LPCHARACTER ch, long x, long y)
	{
		if (!ch)
			return;
		ch->SetRotationToXY(x, y);
		ch->Goto(x, y);
	}

	void WanderNearHunt(LPCHARACTER ch, TPbmlState& state, DWORD dwNow)
	{
		if (!ch || dwNow < state.dwNextWanderTime)
			return;
		state.dwNextWanderTime = dwNow + 4000 + (ch->GetPlayerID() % 2000);
		const int jitter = 600 + (int)(ch->GetPlayerID() % 500);
		long huntX = 0, huntY = 0;
		PbmlGetHuntXY(huntX, huntY);
		WalkTo(ch, huntX + ((ch->GetPlayerID() & 1) ? jitter : -jitter),
				huntY + ((ch->GetPlayerID() & 2) ? jitter : -jitter));
	}

	// Death and revive stay out of the action set: the policy never chooses
	// them. Returning true means "do not pick a hunt action this pulse".
	bool HandleDeath(LPCHARACTER ch, TPbmlState& state, DWORD dwNow, bool& justDied)
	{
		justDied = false;
		if (!ch->IsDead())
		{
			state.dwDeathDetectedTime = 0;
			state.dwNextReviveAttemptTime = 0;
			return false;
		}

		ch->SetVictim(NULL);
		state.dwTargetVID = 0;
		if (state.dwDeathDetectedTime == 0)
		{
			state.dwDeathDetectedTime = dwNow;
			state.dwNextReviveAttemptTime = dwNow + PBML_REVIVE_DELAY_MS;
			justDied = true;
			sys_log(0, "PLAYERBOT_ML: death pid=%u name=%s map=%ld pos=(%ld,%ld)",
					ch->GetPlayerID(), ch->GetName(), ch->GetMapIndex(),
					ch->GetX(), ch->GetY());
			return true;
		}
		if (dwNow < state.dwNextReviveAttemptTime)
			return true;

		state.dwNextReviveAttemptTime = dwNow + 2000;
		interpret_command(ch, "restart_here", strlen("restart_here"));
		if (!ch->IsDead())
		{
			ch->ReviveInvisible(5);
			state.dwDeathDetectedTime = 0;
			state.dwNextReviveAttemptTime = 0;
			state.hasLastObs = false;
			sys_log(0, "PLAYERBOT_ML: revived pid=%u name=%s hp=%d/%d",
					ch->GetPlayerID(), ch->GetName(), ch->GetHP(), ch->GetMaxHP());
		}
		return true;
	}

	void NoteStuck(LPCHARACTER ch, TPbmlState& state, DWORD dwNow)
	{
		if (!ch)
			return;
		if (state.lLastX == ch->GetX() && state.lLastY == ch->GetY())
		{
			if (state.dwStuckSince == 0)
				state.dwStuckSince = dwNow;
		}
		else
		{
			state.dwStuckSince = 0;
			state.lLastX = ch->GetX();
			state.lLastY = ch->GetY();
		}
		if (state.dwStuckSince != 0 && dwNow - state.dwStuckSince > 8000)
		{
			const int step = 800 + (int)(ch->GetPlayerID() % 400);
			WalkTo(ch, ch->GetX() + ((ch->GetPlayerID() & 1) ? step : -step),
					ch->GetY() + ((ch->GetPlayerID() & 2) ? step : -step));
			state.dwStuckSince = dwNow;
		}
	}

	float RewardFromProgress(LPCHARACTER ch, TPbmlState& state)
	{
		if (!ch || !state.hasLastObs)
			return 0.0f;
		float reward = 0.001f;
		const BYTE level = ch->GetLevel();
		const DWORD exp = ch->GetExp();
		if (level > state.bLastLevel)
			reward += 0.5f;
		else if (exp > state.dwLastExp)
			reward += PbmlClamp01((float)(exp - state.dwLastExp) / PBML_EXP_REWARD_SCALE);
		return reward;
	}

	void RememberObs(LPCHARACTER ch, TPbmlState& state, const float* obs, EPbmlAction action)
	{
		if (!ch || !obs)
			return;
		memcpy(state.lastObs, obs, sizeof(state.lastObs));
		state.lastAction = action;
		state.hasLastObs = true;
		state.dwLastExp = ch->GetExp();
		state.bLastLevel = ch->GetLevel();
	}

	void ExecuteAction(LPCHARACTER ch, LPCHARACTER target, TPbmlState& state,
			DWORD dwNow, EPbmlAction action)
	{
		if (!ch)
			return;
		switch (action)
		{
			case PBML_ACT_WAIT:
				ch->Stop();
				ch->SetVictim(NULL);
				break;
			case PBML_ACT_GO_HUNT:
				state.dwTargetVID = 0;
				ch->SetVictim(NULL);
				{
					long huntX = 0, huntY = 0;
					PbmlGetHuntXY(huntX, huntY);
					WalkTo(ch, huntX, huntY);
				}
				break;
			case PBML_ACT_APPROACH:
				if (target)
					WalkTo(ch, target->GetX(), target->GetY());
				else
					WanderNearHunt(ch, state, dwNow);
				break;
			case PBML_ACT_ATTACK:
				if (target)
				{
					ch->Stop();
					Swing(ch, target, state, dwNow);
				}
				else
					WanderNearHunt(ch, state, dwNow);
				break;
			case PBML_ACT_WANDER:
			default:
				ch->SetVictim(NULL);
				WanderNearHunt(ch, state, dwNow);
				break;
		}
	}
}

CPlayerBotMlManager::CPlayerBotMlManager()
{
}

CPlayerBotMlManager::~CPlayerBotMlManager()
{
}

bool CPlayerBotMlManager::Enabled() const
{
	return PbmlEnvOn("PLAYERBOT_ML", true);
}

void CPlayerBotMlManager::Bootstrap()
{
	if (!Enabled())
		return;

	const long village = playerbot_empire_rules::GetHomeMap(
			playerbot_empire_rules::EMPIRE_SHINSOO,
			playerbot_empire_rules::MAP_ROLE_M1);
	if (village == 0 || !map_allow_find(village))
	{
		sys_log(0, "PLAYERBOT_ML: bootstrap skipped (Shinsoo M1 not on this core)");
		return;
	}

	const int want = PbmlEnvInt("PLAYERBOT_ML_AUTOSPAWN_COUNT", PBML_DEFAULT_SPAWN, 0, PBML_MAX_SPAWN);
	if (want <= 0)
	{
		sys_log(0, "PLAYERBOT_ML: bootstrap spawn count is 0");
		return;
	}

	const size_t started = CPlayerBotManager::instance().SpawnRegistered(
			(size_t)want, (BYTE)playerbot_empire_rules::EMPIRE_SHINSOO);
	sys_log(0, "PLAYERBOT_ML: bootstrap requested=%d started=%u (needs M2_PLAYERBOT_KINGDOMS=1 for Shinsoo identities)",
			want, (unsigned int)started);
}

bool CPlayerBotMlManager::TakeOver(LPCHARACTER ch, DWORD dwNow)
{
	if (!ch || !Enabled())
		return false;
	if (!ch->GetDesc() || !ch->GetDesc()->IsBot())
		return false;
	if (ch->GetEmpire() != playerbot_empire_rules::EMPIRE_SHINSOO)
		return false;

	Tick(ch, dwNow);
	return true;
}

void CPlayerBotMlManager::Tick(LPCHARACTER ch, DWORD dwNow)
{
	if (!ch)
		return;

	TPbmlState& state = s_mapState[ch->GetPlayerID()];
	bool justDied = false;
	if (HandleDeath(ch, state, dwNow, justDied))
	{
		if (justDied && state.hasLastObs)
			PbmlReplayAppend(state.lastObs, state.lastAction, -1.0f, true);
		return;
	}
	if (!ch->GetDesc() || !ch->GetDesc()->IsPhase(PHASE_GAME))
		return;

	// Hardcoded: the policy does not drink or revive.
	DrinkPotion(ch, state, dwNow);

	LPCHARACTER target = NULL;
	if (state.dwTargetVID != 0)
		target = CHARACTER_MANAGER::instance().Find(state.dwTargetVID);
	if (!target || target->IsDead() || target->GetMapIndex() != ch->GetMapIndex())
	{
		target = FindNearestHunt(ch);
		state.dwTargetVID = target ? target->GetVID() : 0;
	}

	float obs[PBML_OBS_DIM];
	PbmlFillObservation(ch, target, obs);

	EPbmlAction action = PbmlChooseAction(obs);
	if ((action == PBML_ACT_ATTACK || action == PBML_ACT_APPROACH) && !target)
		action = PBML_ACT_WANDER;

	ExecuteAction(ch, target, state, dwNow, action);
	NoteStuck(ch, state, dwNow);

	PbmlReplayAppend(obs, action, RewardFromProgress(ch, state), false);
	RememberObs(ch, state, obs, action);
}
