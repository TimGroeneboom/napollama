#pragma once

#include <atomic>
#include <blockingconcurrentqueue.h>
#include <condition_variable>
#include <thread>
#include <nap/device.h>

namespace nap
{
    class NAPAPI OllamaChat : public Device
    {
    RTTI_ENABLE(Device)
    public:
        bool start(utility::ErrorState& errorState) override;

        void stop() override;

        void chat(const std::string& message,
                  const std::function<void(const std::string&)>& callback,
                  const std::function<void()>& onComplete);

        void stopChat();

        // properties :
        std::string mModel = "deepseek-r1:14b";
        std::string mServerURL = "http://localhost:11434";
    private:
        std::thread mWorkerThread;
        std::atomic_bool mRunning = true;
        void onWork();

        std::atomic_bool mResponding = false;

        struct Impl;
        std::unique_ptr<Impl> mImpl;

        std::mutex mTaskQueueMutex;
        std::condition_variable mSignalWorkerThreadContinue;
        std::vector<std::function<void()>> mTaskQueue;
    };
}