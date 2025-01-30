// Local Includes
#include "ollamaservice.h"

// External Includes
#include <nap/core.h>
#include <nap/resourcemanager.h>
#include <nap/logger.h>
#include <iostream>

RTTI_BEGIN_CLASS_NO_DEFAULT_CONSTRUCTOR(nap::ollamaService)
	RTTI_CONSTRUCTOR(nap::ServiceConfiguration*)
RTTI_END_CLASS

namespace nap
{
	bool ollamaService::init(nap::utility::ErrorState& errorState)
	{
		//Logger::info("Initializing ollamaService");
		return true;
	}


	void ollamaService::update(double deltaTime)
	{
	}
	

	void ollamaService::getDependentServices(std::vector<rtti::TypeInfo>& dependencies)
	{
	}
	

	void ollamaService::shutdown()
	{
	}
}
