#ifndef __INC_METIN2_PBML_ENV_H__
#define __INC_METIN2_PBML_ENV_H__

#include <cstdlib>
#include <cstring>

inline bool PbmlEnvOn(const char* name, bool defaultOn)
{
	const char* text = std::getenv(name);
	if (!text || !*text)
		return defaultOn;
	if (text[0] == '0' && text[1] == '\0')
		return false;
	if (text[0] == '1' && text[1] == '\0')
		return true;
	return defaultOn;
}

inline int PbmlEnvInt(const char* name, int fallback, int lo, int hi)
{
	const char* text = std::getenv(name);
	if (!text || !*text)
		return fallback;
	int value = std::atoi(text);
	if (value < lo)
		return lo;
	if (value > hi)
		return hi;
	return value;
}

inline const char* PbmlEnvStr(const char* name, const char* fallback)
{
	const char* text = std::getenv(name);
	if (!text || !*text)
		return fallback;
	return text;
}

#endif
