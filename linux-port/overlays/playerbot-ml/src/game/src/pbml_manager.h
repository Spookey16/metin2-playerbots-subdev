#ifndef __INC_METIN2_PBML_MANAGER_H__
#define __INC_METIN2_PBML_MANAGER_H__

// Side module: Shinsoo-only bot policy, kept out of the Chunjo/Jinno AI.
// Since 1.32 vanilla also has a real Shinsoo kingdom AI; PLAYERBOT_ML=0
// leaves those bots on CPlayerBotManager (1.33 villages, shops, hubs).
// This object only answers
// TakeOver() for empire 1, and Bootstrap() to spawn that cohort.
//
// Tick: observe -> PbmlChooseAction (MLP or scripted) -> execute.
// Death/revive and potions are hardcoded gates, not policy actions.
// Replay is (obs, action, reward) when PLAYERBOT_ML_REPLAY=1.
//
// No playerbot_*.h fragments, no anonymous-namespace includes. The vanilla
// tick calls TakeOver at the top of its per-bot loop; a true return means
// "this character is ours, skip the rest of the vanilla AI".

class CPlayerBotMlManager : public singleton<CPlayerBotMlManager>
{
	public:
		CPlayerBotMlManager();
		~CPlayerBotMlManager();

		bool	Enabled() const;
		void	Bootstrap();
		// True: this bot is Shinsoo under ML and has been ticked. Vanilla
		// must continue to the next descriptor.
		bool	TakeOver(LPCHARACTER ch, DWORD dwNow);

	private:
		void	Tick(LPCHARACTER ch, DWORD dwNow);
};

#endif
