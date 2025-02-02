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
        // Execute tasks on the main thread queued
        if(mMainThreadTaskQueue.size_approx() > 0)
        {
            Task task;
            while (mMainThreadTaskQueue.try_dequeue(task))
                task();
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


    void OllamaChat::chatAsync(const std::string& message,
                               const std::function<void(const std::string&)>& callback,
                               const std::function<void()>& onComplete,
                               const std::function<void(const std::string&)>& onError)
    {
        // Enqueue the chat blocking task to be executed by the worker thread
        enqueueWorkerTask([this, message, callback, onComplete, onError]()
                          {
                              chatBlocking(message, callback, onComplete, onError);
                          });
    }


    void OllamaChat::chat(const std::string &message, const std::function<void(const std::string &)> &callback,
                          const std::function<void()> &onComplete,
                          const std::function<void(const std::string &)> &onError)
    {
        enqueueWorkerTask([this, message, callback, onComplete, onError]()
                          {
                              chatBlocking(message,
                                           [this, callback](const std::string &response)
                                           {
                                               // Execute the callback on the main thread
                                               enqueueMainThreadTask([callback, response]()
                                                                     { callback(response); });
                                           },
                                           [this, onComplete]()
                                           {
                                               // Execute the onComplete callback on the main thread
                                               enqueueMainThreadTask(onComplete);
                                           },
                                           [this, onError](const std::string &error)
                                           {
                                               // Execute the onError callback on the main thread
                                               enqueueMainThreadTask([onError, error]()
                                                                     { onError(error); });
                                           });
                          });
    }


    void OllamaChat::chatBlocking(const std::string &message,
                                  const std::function<void(const std::string &)> &callback,
                                  const std::function<void()> &onComplete,
                                  const std::function<void(const std::string &)> &onError)
    {
        try
        {
            // The ollama server is responding
            mStreaming = true;

            // Get the current context
            ollama::response context = getContext();

            // Prompt the server to generate a response,
            // callback handles the responses (response is one token at a time)
            // this function will block until the response is complete
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
            std::vector<Task> task_queue;
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
                mSignalWorkerThreadContinue.wait(lock, [this]{ return !mWorkerThreadTaskQueue.empty() || !mRunning; });
        }
    }


    void OllamaChat::enqueueWorkerTask(const Task& task)
    {
        // Enqueue the task to be executed on worker thread
        {
            std::unique_lock lk(mTaskQueueMutex);
            mWorkerThreadTaskQueue.emplace_back(task);
        }

        // Notify the worker thread to continue
        mSignalWorkerThreadContinue.notify_one();
    }


    void OllamaChat::enqueueMainThreadTask(const Task& task)
    {
        // Enqueue the task to be executed on the main thread
        mMainThreadTaskQueue.enqueue(task);
    }
}