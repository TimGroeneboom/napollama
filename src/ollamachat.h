#pragma once

#include "ollamaservice.h"

#include <atomic>
#include <blockingconcurrentqueue.h>
#include <condition_variable>
#include <thread>
#include <nap/device.h>

// Forward declarations
namespace ollama
{
    class response;
}

namespace nap
{
    /**
     * OllamaChat is a device that maintains a conversation with the Ollama AI.
     * OllamaChat will fail to start if Ollama server is not running or if the model is not found.
     */
    class NAPAPI OllamaChat final : public Device
    {
        friend class OllamaService;

    RTTI_ENABLE(Device)
    public:
        /**
         * Constructor
         * @param service reference to the Ollama service
         */
        OllamaChat(OllamaService& service);

        /**
         * Destructor
         */
        virtual ~OllamaChat();

        /**
         * Generate a prompt with the given message
         * The callback will get called by each given token in the response
         * All callbacks are executed on the calling thread
         * @param message the message to prompt
         * @param callback the callback that gets called for each token in the response
         * @param onComplete the callback that gets called when the response is complete
         * @param onError the callback that gets called on error
         */
        void chat(const std::string& message,
                  const std::function<void(const std::string&)>& callback,
                  const std::function<void()>& onComplete,
                  const std::function<void(const std::string&)>& onError);

        /**
         * Generate a prompt with the given message
         * The callback will get called by each given token in the response
         * All callbacks are executed on the worker thread
         * @param message the message to prompt
         * @param callback the callback that gets called for each token in the response
         * @param onComplete the callback that gets called when the response is complete
         * @param onError the callback that gets called on error
         */
        void chatAsync(const std::string& message,
                       const std::function<void(const std::string&)>& callback,
                       const std::function<void()>& onComplete,
                       const std::function<void(const std::string&)>& onError);

        /**
         * Clear the context for the next chat message
         */
        void clearContext();

        /**
         * Stop current response
         * This will close the http connection to the server effectively stopping the response
         * This call is thread safe
         */
        void stopResponse();

        // properties :
        std::string mModelSetting = "deepseek-r1:14b"; ///< Property : 'Model' The model to use for the chat
        std::string mServerURLSetting = "http://localhost:11434"; ///< Property : 'ServerURL' The URL of the Ollama server
    protected:
        /**
         * Starts the OllamaChat device, start worker thread, checks if model is available and if server is running
         * @param errorState contains the error message on failure
         * @return true on success
         */
        bool start(utility::ErrorState& errorState) final;

        /**
         * Stops the OllamaChat device, stops worker thread
         */
        void stop() final;
    private:
        // Task type, shorthand for a function that takes no arguments and returns void
        using Task = std::function<void()>;

        /**
         * Generate a prompt with the given message
         * The callback will get called by each given token in the response
         * All callbacks are executed on the calling thread
         * This call will block until the response is complete
         * @param message the message to prompt
         * @param callback the callback that gets called for each token in the response
         * @param onComplete the callback that gets called when the response is complete
         * @param onError the callback that gets called on error
         */
        void chatBlocking(const std::string& message,
                          const std::function<void(const std::string&)>& callback,
                          const std::function<void()>& onComplete,
                          const std::function<void(const std::string&)>& onError);

        /**
         * Updates the OllamaChat device, called on main thread from OllamaService
         */
        void update();

        // worker thread that handles the chat
        std::thread mWorkerThread;
        void onWork();

        /**
         * Sets the context for the next chat message
         * @param context
         */
        void setContext(const std::string& context);

        /**
         * Sets the context for the next chat message
         * @param context
         */
        void setContext(const ollama::response& context);

        /**
         * Gets the context for the next chat message
         * @return the context
         */
        ollama::response getContext();

        /**
         * Enqueues a task to be executed on the worker thread
         * @param task the task to execute
         */
        void enqueueWorkerTask(const Task& task);

        /**
         * Enqueues a task to be executed on the main thread called from update() from OllamaService
         * @param task the task to execute
         */
        void enqueueMainThreadTask(const Task& task);

        // mutex for the context
        std::mutex mContextMutex;

        // atomic bool indicating if the worker thread is currently streaming a response
        std::atomic_bool mStreaming = false;

        // atomic bool indicating if the worker thread is running
        std::atomic_bool mRunning = true;

        // mutex for the task queue that are executed on the worker thread
        std::mutex mTaskQueueMutex;

        // task queue that are executed on the worker thread
        std::vector<Task> mWorkerThreadTaskQueue;

        // condition variable to signal the worker thread to continue
        std::condition_variable mSignalWorkerThreadContinue;

        // pimpl ollama implementation defined in ollamachat.cpp
        struct Impl;
        std::unique_ptr<Impl> mImpl;

        // service that manages the OllamaChat device
        OllamaService& mService;

        // queue of tasks to execute on the main thread
        moodycamel::ConcurrentQueue<Task> mMainThreadTaskQueue;

        std::string mModel; ///< The model to use for the chat
        std::string mServerURL; ///< The URL of the Ollama server
    };

    using OllamaChatObjectCreator = rtti::ObjectCreator<OllamaChat, OllamaService>;
}