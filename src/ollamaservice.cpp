// Local Includes
#include "ollamaservice.h"
#include "ollamachat.h"

// External Includes
#include <nap/core.h>
#include <nap/resourcemanager.h>
#include <nap/logger.h>
#include <iostream>

RTTI_BEGIN_CLASS_NO_DEFAULT_CONSTRUCTOR(nap::OllamaService)
	RTTI_CONSTRUCTOR(nap::ServiceConfiguration*)
RTTI_END_CLASS

namespace nap
{
	bool OllamaService::init(nap::utility::ErrorState& errorState)
	{
		//Logger::info("Initializing ollamaService");
		return true;
	}


	void OllamaService::update(double deltaTime)
	{
        for(auto chat : mChats)
        {
            chat->update();
        }
	}
	

	void OllamaService::getDependentServices(std::vector<rtti::TypeInfo>& dependencies)
	{
	}
	

	void OllamaService::shutdown()
	{
	}


    void OllamaService::registerChat(OllamaChat& chat)
    {
        mChats.push_back(&chat);
    }


    void OllamaService::removeChat(OllamaChat& chat)
    {
        auto it = std::find(mChats.begin(), mChats.end(), &chat);
        if(it != mChats.end())
        {
            mChats.erase(it);
        }
    }


    void OllamaService::registerObjectCreators(rtti::Factory &factory)
    {
        factory.addObjectCreator(std::make_unique<OllamaChatObjectCreator>(*this));
    }
}
