#ifndef _INCLUDE_SOURCEMOD_EXTENSION_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_H_

#include "smsdk_ext.h"

class PhysSwapperExtension : public SDKExtension
{
public:
	virtual bool SDK_OnLoad(char *error, size_t maxlen, bool late) override;
	virtual void SDK_OnUnload() override;
};

#endif