// Local Includes
#include "ollamademoservice.h"

// External Includes
#include <nap/core.h>
#include <nap/resourcemanager.h>
#include <nap/logger.h>
#include <iostream>

RTTI_BEGIN_CLASS_NO_DEFAULT_CONSTRUCTOR(nap::ollamademoService)
	RTTI_CONSTRUCTOR(nap::ServiceConfiguration*)
RTTI_END_CLASS

namespace nap
{
	bool ollamademoService::init(nap::utility::ErrorState& errorState)
	{
		//Logger::info("Initializing ollamademoService");
		return true;
	}


	void ollamademoService::update(double deltaTime)
	{
	}
	

	void ollamademoService::getDependentServices(std::vector<rtti::TypeInfo>& dependencies)
	{
	}
	

	void ollamademoService::shutdown()
	{
	}
}
