#ifndef __INC_METIN2_PBML_WORLD_H__
#define __INC_METIN2_PBML_WORLD_H__

#include "pbml_types.h"
#include "playerbot_empire_rules.h"

// Town and hunt points for Shinsoo. Town comes from the 1.32 kingdom catalog
// (Yongan's blacksmith). Hunt is a measured M1 spawn hub, kept here rather
// than by including playerbot_types.h (that file is an anonymous-namespace
// fragment and must not be pulled into this TU).

inline void PbmlGetTownXY(long& x, long& y)
{
	playerbot_empire_rules::TTownServices services;
	const long village = playerbot_empire_rules::GetHomeMap(
			playerbot_empire_rules::EMPIRE_SHINSOO,
			playerbot_empire_rules::MAP_ROLE_M1);
	if (village != 0 && playerbot_empire_rules::GetTownServices(village, services))
	{
		x = services.blacksmith.x;
		y = services.blacksmith.y;
		return;
	}
	x = PBML_TOWN_X;
	y = PBML_TOWN_Y;
}

inline void PbmlGetHuntXY(long& x, long& y)
{
	x = PBML_HUNT_X;
	y = PBML_HUNT_Y;
}

#endif
