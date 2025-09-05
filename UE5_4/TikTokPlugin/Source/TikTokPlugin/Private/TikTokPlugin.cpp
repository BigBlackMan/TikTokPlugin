// Copyright Epic Games, Inc. All Rights Reserved.
// TikTokPlugin.cpp

#include "TikTokPlugin.h" // Module declaration
#include "WebSocketConnection.h" // Worker declaration
#include "Interfaces/IPluginManager.h"
#include "GameplayTagsManager.h"

#define LOCTEXT_NAMESPACE "FTikTokPluginModule"

void FTikTokPluginModule::StartupModule() // Module load
{
    // No auto-connect. We only connect when the BP node is called.
    UE_LOG(LogTemp, Log, TEXT("TikTokPlugin: Module started successfully.")); // Info log
    const TSharedPtr<IPlugin> ThisPlugin = IPluginManager::Get().FindPlugin(TEXT("TikTokPlugin"));
    if (ThisPlugin.IsValid())
    {
        const FString BaseDir = ThisPlugin->GetBaseDir();

        // Register both .../Config/Tags (legacy list files) and .../Config (if you ever ship DefaultGameplayTags.ini)
        UGameplayTagsManager& Tags = UGameplayTagsManager::Get();
        Tags.AddTagIniSearchPath(BaseDir / TEXT("Config/Tags"));
        Tags.AddTagIniSearchPath(BaseDir / TEXT("Config"));
    }
}

void FTikTokPluginModule::ShutdownModule() // Module unload
{
    // Ensure graceful cleanup if a connection was started
    StopConnection(); // Stop + join + delete worker if necessary
}

#undef LOCTEXT_NAMESPACE

FTikTokPluginModule& FTikTokPluginModule::Get() // Accessor to module instance
{
    return FModuleManager::LoadModuleChecked<FTikTokPluginModule>(TEXT("TikTokPlugin")); // Safe accessor
}

void FTikTokPluginModule::StartConnection(const FString& InHandle) // Start on-demand
{
    if (!WebSocketConnection) // Create worker if missing
    {
        WebSocketConnection = new FWebSocketConnection(); // Allocate worker
    }

    if (WebSocketConnection->IsRunning()) // Prevent duplicate start
    {
        UE_LOG(LogTemp, Warning, TEXT("TikTokPlugin: Connection already running.")); // Warn and no-op
        return; // Already running
    }

    WebSocketConnection->Start(InHandle); // Start worker thread with the provided handle
}

void FTikTokPluginModule::StopConnection() // Stop on-demand
{
    if (WebSocketConnection) // If created
    {
        if (WebSocketConnection->IsRunning()) // If thread is active
        {
            WebSocketConnection->Stop(); // Signal stop
            WebSocketConnection->Join(); // Join thread
        }
        delete WebSocketConnection; // Free worker
        WebSocketConnection = nullptr; // Null pointer
    }
}

bool FTikTokPluginModule::IsConnectionRunning() const // Query running state
{
    return WebSocketConnection && WebSocketConnection->IsRunning(); // True if worker exists and has a thread
}

IMPLEMENT_MODULE(FTikTokPluginModule, TikTokPlugin) // UE module boilerplate
