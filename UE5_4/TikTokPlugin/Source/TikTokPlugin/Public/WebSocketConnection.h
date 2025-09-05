// WebSocketConnection.h

#pragma once // Include guard

#include "CoreMinimal.h" // UE core types
#include "HAL/Runnable.h" // FRunnable interface
#include "HAL/RunnableThread.h" // FRunnableThread
#include <atomic> // For atomic flags (harmless, standard lib)

class FWebSocketConnection : public FRunnable // Stubbed class to preserve interface
{
public:
    FWebSocketConnection() {} // Empty constructor
    ~FWebSocketConnection() { Join(); } // Ensure join on destruction

    virtual bool Init() override { return true; } // No-op init
    virtual uint32 Run() override { return 0; } // No worker logic in stub
    virtual void Stop() override { bIsStopping = true; } // Set stop flag

    void Start(const FString& /*InHandle*/) // Stubbed start
    {
        if (!Thread) // If no thread yet
        {
            // Spawn a tiny do-nothing thread so calls don't crash, but avoid third-party includes
            Thread = FRunnableThread::Create(this, TEXT("StubbedWebSocketThread"), 0, TPri_BelowNormal); // Create thread
        }
    }

    void Join() // Join stub thread
    {
        if (Thread) // If thread exists
        {
            Thread->WaitForCompletion(); // Join
            delete Thread; // Free thread
            Thread = nullptr; // Null out pointer
        }
    }

    bool IsRunning() const { return Thread != nullptr; } // Running if thread exists

private:
    FRunnableThread* Thread = nullptr; // Stub thread pointer
    std::atomic<bool> bIsStopping{ false }; // Stop flag (unused in stub)
};
