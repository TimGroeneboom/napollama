#include "ollamachat.h"
#include "ollamaservice.h"

#include "ollama.hpp"
#include "nap/logger.h"

RTTI_BEGIN_CLASS_NO_DEFAULT_CONSTRUCTOR(nap::OllamaChat)
    RTTI_CONSTRUCTOR(nap::OllamaService&)
    RTTI_PROPERTY("ServerURL", &nap::OllamaChat::mServerURLSetting, nap::rtti::EPropertyMetaData::Default)
    RTTI_PROPERTY("Model", &nap::OllamaChat::mModelSetting, nap::rtti::EPropertyMetaData::Default)
RTTI_END_CLASS

namespace nap
{
    /**
     * OllamaChat implementation
     */
    struct OllamaChat::Impl
    {
        // Ollama server
        std::unique_ptr<Ollama> mServer;

        // Context for the next chat message
        ollama::response mContext;
    };


    OllamaChat::OllamaChat(OllamaService& service) : Device(), mService(service)
    { }


    OllamaChat::~OllamaChat()
    { }


    bool OllamaChat::start(utility::ErrorState& errorState)
    {
        // Copy the server URL & model
        mServerURL = mServerURLSetting;
        mModel = mModelSetting;

        // Create the ollama server
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

        // Start the worker thread
        mRunning = true;
        mWorkerThread = std::thread([this] { onWork(); });

        // Register the chat with the ollama service
        mService.registerChat(*this);

        return true;
    }


    void OllamaChat::stop()
    {
        // Stop the worker thread & join
        stopResponse();
        mRunning = false;
        mSignalWorkerThreadContinue.notify_one();
        mWorkerThread.join();

        // Unregister the chat with the ollama service
        mService.removeChat(*this);
    }


    void OllamaChat::update()
    {
        // Execute tasks on the main thread queued by the worker thread
        if(mMainThreadTaskQueue.size_approx() > 0)
        {
            std::function<void()> task;
            while (mMainThreadTaskQueue.try_dequeue(task))
            {
                task();
            }
        }
    }


    void OllamaChat::stopResponse()
    {
        // Only stop if we are streaming
        if (mStreaming)
        {
            // Stop is effectively closing the http connection
            mImpl->mServer->stop();
            mStreaming = false;
        }
    }


    void OllamaChat::chat(const std::string &message, const std::function<void(const std::string &)> &callback,
                          const std::function<void()> &onComplete,
                          const std::function<void(const std::string &)> &onError)
    {
        // Create a task to be executed by the worker thread
        std::function function([this, message, callback, onComplete, onError]()
        {
            try
            {
               // The ollama server is responding
               mStreaming = true;

               // Get the current context
               ollama::response context = getContext();

               // Prompt the server to generate a response,
               // callback handles the responses (response is one token at a time)
               mImpl->mServer->generate(mModel,
                                        message,
                                        context,
                                        [this, callback, onComplete](const ollama::response& response)
               {
                    // The last response is the context for the next prompt
                    setContext(response);

                    // Call the callback with the response on the main thread
                    std::string response_str = response;
                    mMainThreadTaskQueue.enqueue([response_str, callback](){ callback(response_str); });

                    // If the response is done, call the onComplete callback
                    if (response.as_json()["done"]==true)
                    {
                        mMainThreadTaskQueue.enqueue([onComplete]() { onComplete(); });
                        mStreaming = false;
                    }
                });
            }catch (const std::exception& exception)
            {
               // Call onError callback on error
                std::string error = exception.what();
                mStreaming = false;
                mMainThreadTaskQueue.enqueue([onError, error](){ onError(error); });
            }
        });

        enqueueTask(function);
    }


    void OllamaChat::chatAsync(const std::string& message,
                               const std::function<void(const std::string&)>& callback,
                               const std::function<void()>& onComplete,
                               const std::function<void(const std::string&)>& onError)
    {
        // Create a task to be executed by the worker thread
        std::function function([this, message, callback, onComplete, onError]()
        {
            try
            {
                // The ollama server is responding
                mStreaming = true;

                // Get the current context
                ollama::response context = getContext();

                // Prompt the server to generate a response,
                // callback handles the responses (response is one token at a time)
                mImpl->mServer->generate(mModel,
                                         message,
                                         context,
                                         [this, callback, onComplete](const ollama::response& response)
                {
                    // The last response is the context for the next prompt
                    setContext(response);

                    // Call the callback with the response
                    std::string response_str = response;
                    callback(response_str);

                    // If the response is done, call the onComplete callback
                    if (response.as_json()["done"]==true)
                    {
                        onComplete();
                        mStreaming = false;
                    }
                });
            }catch (const std::exception& exception)
            {
                // Call onError callback on error
                std::string error = exception.what();
                mStreaming = false;
                onError(error);
            }
        });

        enqueueTask(function);
    }


    void OllamaChat::clearContext()
    {
        setContext("");
    }


    void OllamaChat::setContext(const std::string& context)
    {
        std::lock_guard lk(mContextMutex);
        mImpl->mContext = ollama::response(context);
    }


    ollama::response OllamaChat::getContext()
    {
        std::lock_guard lk(mContextMutex);
        return mImpl->mContext;
    }


    void OllamaChat::setContext(const ollama::response& context)
    {
        std::lock_guard lk(mContextMutex);
        mImpl->mContext = context;
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
                task_queue.swap(mWorkerThreadTaskQueue);
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
            if (mWorkerThreadTaskQueue.empty())
                mSignalWorkerThreadContinue.wait(lock, []{ return true; });
        }
    }


    void OllamaChat::enqueueTask(const std::function<void()>& task)
    {
        // Enqueue the task to be executed on worker thread
        {
            std::unique_lock lk(mTaskQueueMutex);
            mWorkerThreadTaskQueue.emplace_back(task);
        }

        // Notify the worker thread to continue
        mSignalWorkerThreadContinue.notify_one();
    }
}