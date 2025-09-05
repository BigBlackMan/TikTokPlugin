// Copyright Epic Games, Inc. All Rights Reserved.
// TikTokPlugin.h

#pragma once // Prevent multiple inclusion of this header

#include "CoreMinimal.h" // Core UE types/macros
#include "Modules/ModuleManager.h" // Module interface utilities

class FWebSocketConnection; // Forward declaration to avoid heavy includes

class FTikTokPluginModule : public IModuleInterface // UE module interface
{
public:
    virtual void StartupModule() override; // Called when the module is loaded
    virtual void ShutdownModule() override; // Called when the module is unloaded

    static FTikTokPluginModule& Get(); // Helper accessor to loaded module instance

    void StartConnection(const FString& InHandle); // Start websocket worker on demand with a TikTok handle
    void StopConnection(); // Stop the worker if running
    bool IsConnectionRunning() const; // Query whether the worker thread exists

private:
    FWebSocketConnection* WebSocketConnection = nullptr; // Owned worker pointer
};
