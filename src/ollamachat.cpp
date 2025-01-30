#include "ollamachat.h"

#include "ollama.hpp"
#include "nap/logger.h"

RTTI_BEGIN_CLASS(nap::OllamaChat)
    RTTI_PROPERTY("ServerURL", &nap::OllamaChat::mServerURL, nap::rtti::EPropertyMetaData::Default)
    RTTI_PROPERTY("Model", &nap::OllamaChat::mModel, nap::rtti::EPropertyMetaData::Default)
RTTI_END_CLASS

namespace nap
{
    struct OllamaChat::Impl
    {
        std::unique_ptr<Ollama> mServer;
        ollama::response mContext;
    };


    bool OllamaChat::start(utility::ErrorState& errorState)
    {
        mImpl = std::make_unique<Impl>();
        mImpl->mServer = std::make_unique<Ollama>(mServerURL);

        // check if server is running
        if (!errorState.check(mImpl->mServer->is_running(), "Ollama server is not running!"))
            return false;

        // check if model is available
        auto models = mImpl->mServer->list_models();
        auto it = std::find_if(models.begin(), models.end(), [this](const std::string& model) { return model == mModel; });
        if (!errorState.check(it != models.end(), utility::stringFormat("%s model not found!", mModel.c_str())))
        {
            mImpl->mServer->list_models();
            nap::Logger::info("Models found : ");
            for (const auto& model : models)
            {
                nap::Logger::info(" ---- %s", model.c_str());
            }

            return false;
        }

        mRunning = true;
        mWorkerThread = std::thread([this] { onWork(); });
        return true;
    }


    void OllamaChat::stop()
    {
        stopChat();
        mRunning = false;
        mSignalWorkerThreadContinue.notify_one();
        mWorkerThread.join();
    }


    void OllamaChat::stopChat()
    {
        // Only stop if we are responding
        if (mResponding)
        {
            // Stop is effectively closing the http connection
            mImpl->mServer->stop();
            mResponding = false;
        }
    }



    void OllamaChat::chat(const std::string& message,
                          const std::function<void(const std::string&)>& callback,
                          const std::function<void()>& onComplete)
    {
        // Create a task to be executed by the worker thread
        std::function function([this, message, callback, onComplete]()
        {
            try
            {
                // The ollama server is responding
                mResponding = true;

                // Prompt the server to generate a response,
                // callback handles the responses (response is one token at a time)
                mImpl->mServer->generate(mModel,
                                         message,
                                         mImpl->mContext,
                                         [this, callback, onComplete](const ollama::response& response)
                {
                    // The last response is the context for the next prompt
                    mImpl->mContext = response;

                    // Call the callback with the response
                    callback(response);

                    // If the response is done, call the onComplete callback
                    if (response.as_json()["done"]==true)
                    {
                        onComplete();
                        mResponding = false;
                    }
                });
            }catch (std::exception& exception)
            {
                // Log the error and call the onComplete callback
                nap::Logger::error(exception.what());
                mResponding = false;
                onComplete();
            }
        });

        // Enqueue the task to be executed on worker thread
        {
            std::unique_lock lk(mTaskQueueMutex);
            mTaskQueue.emplace_back(function);
        }

        // Notify the worker thread to continue
        mSignalWorkerThreadContinue.notify_one();
    }


    void OllamaChat::onWork()
    {
        // Worker thread loop
        while (mRunning)
        {
            // Swap the task queue to avoid locking the mutex for too long
            std::vector<std::function<void()>> task_queue;
            {
                std::unique_lock lock(mTaskQueueMutex);
                task_queue.swap(mTaskQueue);
            }

            // Execute tasks
            for (auto& task : task_queue)
            {
                task();

                // Bail out if we are no longer running
                if (!mRunning)
                    return;
            }

            // Wait for more tasks
            std::unique_lock lock(mTaskQueueMutex);
            if (mTaskQueue.empty())
                mSignalWorkerThreadContinue.wait(lock, []{ return true; });
        }
    }
}