// Copyright Epic Games, Inc. All Rights Reserved.
// TikTokPluginBPLibrary.h
#pragma once // Header guard to prevent multiple inclusion

#include "Kismet/BlueprintFunctionLibrary.h"      // Base class for BP libraries
#include "TikTokTeams.h"                          // Team enums for mode selection
#include "TikTokGiftTypes.h"                      // Gift auto-queue enums (EGiftAutoQueueMode)
#include "TikTokPluginBPLibrary.generated.h"      // UHT glue

// Forward declarations (avoid heavy includes in headers)
class UTikTokPluginEventHandler;                  // Event handler object
class UTikTokUserRegistry;                        // User registry singleton
class UTikTokGiftQueueManager;                    // Gift queue manager 

UCLASS()                                          
class UTikTokPluginBPLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_UCLASS_BODY()                       

public:
    // Connect to TikTok Live (requires API key; plus avatar cache toggle, team mode, and gift auto-queue mode)
    UFUNCTION(BlueprintCallable,
        meta = (DisplayName = "Connect to TikTok Live",                               
            Keywords = "TikTok Live Connect EulerStream API key handle gifts",  
            CPP_Default_bAutoCacheProfilePictures = "false",                      
            CPP_Default_TeamMode = "None",                                        
            CPP_Default_AutoQueueMode = "None"),                                  
        Category = "TikTok|Live")
        static void ConnectToTikTokLive(
        const FString& Handle,            // TikTok @handle
		const FString& ApiKey,            // EulerStream API key entered by user (EulerStream.com)
        bool bAutoCacheProfilePictures,   // If true, avatars download & cache automatically
        ETikTokTeamMode TeamMode,         // Initial team mode selection
        EGiftAutoQueueMode AutoQueueMode);// How gifts are auto-queued (None / ByName / ByValueBucket)

	// Disconnect from TikTok Live (graceful shutdown; can be called even if not connected)
    UFUNCTION(BlueprintCallable,
        meta = (DisplayName = "Disconnect from TikTok Live",                          
        Keywords = "TikTok Live Disconnect EulerStream"),                   
        Category = "TikTok|Live")
    static void DisconnectFromTikTokLive();                                           

    // Sample (unchanged)
    UFUNCTION(BlueprintCallable,
        meta = (DisplayName = "Execute Sample function",                              
        Keywords = "TikTokPlugin sample test testing"),                     
        Category = "TikTokPluginTesting")
    static float TikTokPluginSampleFunction(float Param);                             

    // Event handler singleton for BP bindings
    UFUNCTION(BlueprintCallable, Category = "TikTok|Events",
        meta = (DisplayName = "Get TikTok Event Handler",                             
        Keywords = "TikTok events handler like"))                           
        static UTikTokPluginEventHandler* GetTikTokEventHandler();                        

	// Registry singleton, access to user records
    UFUNCTION(BlueprintCallable, Category = "TikTok|Users",
        meta = (DisplayName = "Get TikTok User Registry"))                            
        static UTikTokUserRegistry* GetUserRegistry();                                    

    // Gift Queue Manager singleton, access to gift queues and functions
    UFUNCTION(BlueprintCallable, Category = "TikTok|Gifts",
        meta = (DisplayName = "Get Gift Queue Manager"))                              
        static UTikTokGiftQueueManager* GetGiftQueueManager();                            

    // Convenience function to control team mode mid-session
    UFUNCTION(BlueprintCallable, Category = "TikTok|Teams",
        meta = (DisplayName = "Set Team Mode"))                                       
        static void SetTeamMode(ETikTokTeamMode InMode);                                  

    // Get current team mode
    UFUNCTION(BlueprintPure, Category = "TikTok|Teams",
        meta = (DisplayName = "Get Team Mode"))                                       
        static ETikTokTeamMode GetTeamMode();                                             

private:
    static UTikTokPluginEventHandler* StaticEventHandler;                             // Global event handler pointer (rooted)
};
