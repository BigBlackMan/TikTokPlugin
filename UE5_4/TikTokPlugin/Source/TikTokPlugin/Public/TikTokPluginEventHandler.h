// TikTokPluginEventHandler.h
#pragma once // Prevent multiple inclusion

#include "CoreMinimal.h"                 // Core UE types/macros
#include "UObject/NoExportTypes.h"       // Minimal UObject base class

// NEW: bring in user record and gift payload types for delegate params
#include "TikTokUserRegistry.h"          // FTikTokUserRecord
#include "TikTokGiftStructs.h"           // FTikTokQueuedGift

#include "TikTokPluginEventHandler.generated.h" // UHT glue

// ----- Delegates (Blueprint-assignable) for message events ----- //

// Expanded: include full user record and the full queued gift payload
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SevenParams( // 7-param delegate for gifts
    FOnGiftEvent,
    const FString&, Username,             // Display name or uniqueId
    const FString&, UserID,               // Stable user id
    const FString&, UserProfilePicURL,    // Avatar URL (may be empty)
    const FString&, GiftType,             // Gift name (e.g., "Rose")
    int32, GiftValue,                     // Diamond count for this message
    const FTikTokUserRecord&, UserRecord, // Current user record (registry snapshot)
    const FTikTokQueuedGift&, Gift        // Full gift payload (enqueue snapshot)
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(  // 5-param delegate for comments
    FOnCommentEvent,
    const FString&, Username,             // Display name or uniqueId
    const FString&, UserID,               // Stable user id
    const FString&, UserProfilePicURL,    // Avatar URL
    const FString&, CommentText,          // Comment text
    const FTikTokUserRecord&, UserRecord  // Current user record
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(  // 5-param delegate for likes
    FOnLikeEvent,
    const FString&, Username,             // Display name or uniqueId
    const FString&, UserID,               // Stable user id
    const FString&, UserProfilePicURL,    // Avatar URL
    int32, LikeCount,                     // Like count for this message
    const FTikTokUserRecord&, UserRecord  // Current user record
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(  // 5-param delegate for follows
    FOnFollowEvent,
    const FString&, Username,             // Display name or uniqueId
    const FString&, UserID,               // Stable user id
    const FString&, UserProfilePicURL,    // Avatar URL
    bool, AlreadyFollowed,                // True if we had already seen a follow
    const FTikTokUserRecord&, UserRecord  // Current user record
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(  // 5-param delegate for shares
    FOnShareEvent,
    const FString&, Username,             // Display name or uniqueId
    const FString&, UserID,               // Stable user id
    const FString&, UserProfilePicURL,    // Avatar URL
    int32, Shares,                        // Shares count (1 per message)
    const FTikTokUserRecord&, UserRecord  // Current user record
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(   // 2-param delegate for room info
    FOnRoomInfoEvent,
    const FString&, RoomTitle,            // Not always provided (may be empty)
    int32, ViewerCount                    // Viewer count snapshot
);

// join event (fires when a viewer joins / SyntheticJoinMessage)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
    FOnJoinEvent,
    const FString&, Username,             // Display name or uniqueId
    const FString&, UserID,               // Stable user id
    const FString&, UserProfilePicURL,    // Avatar URL
    const FTikTokUserRecord&, UserRecord  // Current user record
);

// connection lifecycle events
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnDisconnected,
    const FString&, Reason                // Human-friendly reason string
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(
    FOnReconnected                        // Fired after a successful auto-reconnect
);

UCLASS(Blueprintable) // Allow binding in Blueprints
class UTikTokPluginEventHandler : public UObject
{
    GENERATED_BODY()                      // UHT boilerplate

public:
    // ----- Blueprint events (bind to these) ----- //
    UPROPERTY(BlueprintAssignable, Category = "TikTokPlugin|Events")
    FOnGiftEvent OnGiftEvent;             // Gift event multicast

    UPROPERTY(BlueprintAssignable, Category = "TikTokPlugin|Events")
    FOnCommentEvent OnCommentEvent;       // Comment event multicast

    UPROPERTY(BlueprintAssignable, Category = "TikTokPlugin|Events")
    FOnLikeEvent OnLikeEvent;             // Like event multicast

    UPROPERTY(BlueprintAssignable, Category = "TikTokPlugin|Events")
    FOnFollowEvent OnFollowEvent;         // Follow event multicast

    UPROPERTY(BlueprintAssignable, Category = "TikTokPlugin|Events")
    FOnShareEvent OnShareEvent;           // Share event multicast

    UPROPERTY(BlueprintAssignable, Category = "TikTokPlugin|Events")
    FOnRoomInfoEvent OnRoomInfoEvent;     // Room info event multicast

    UPROPERTY(BlueprintAssignable, Category = "TikTokPlugin|Events")
    FOnJoinEvent OnJoinEvent;             // Join event multicast

    // connection lifecycle delegates you can bind to in BP
    UPROPERTY(BlueprintAssignable, Category = "TikTokPlugin|Connection")
    FOnDisconnected OnDisconnected;       // Fires on manual or unexpected disconnect

    UPROPERTY(BlueprintAssignable, Category = "TikTokPlugin|Connection")
    FOnReconnected OnReconnected;         // Fires after a successful auto-reconnect

    // ----- C++ helpers to broadcast (called by parser on the GameThread) ----- //
    UFUNCTION(BlueprintCallable, Category = "TikTokPlugin|Events")
    void TriggerGiftEvent(
        const FString& Username,
        const FString& UserID,
        const FString& UserProfilePicURL,
        const FString& GiftType,
        int32 GiftValue,
        const FTikTokUserRecord& UserRecord,
        const FTikTokQueuedGift& Gift)
    {
        OnGiftEvent.Broadcast(Username, UserID, UserProfilePicURL, GiftType, GiftValue, UserRecord, Gift); // Notify listeners
    }

    UFUNCTION(BlueprintCallable, Category = "TikTokPlugin|Events")
    void TriggerCommentEvent(
        const FString& Username,
        const FString& UserID,
        const FString& UserProfilePicURL,
        const FString& CommentText,
        const FTikTokUserRecord& UserRecord)
    {
        OnCommentEvent.Broadcast(Username, UserID, UserProfilePicURL, CommentText, UserRecord); // Notify listeners
    }

    UFUNCTION(BlueprintCallable, Category = "TikTokPlugin|Events")
    void TriggerLikeEvent(
        const FString& Username,
        const FString& UserID,
        const FString& UserProfilePicURL,
        int32 LikeCount,
        const FTikTokUserRecord& UserRecord)
    {
        OnLikeEvent.Broadcast(Username, UserID, UserProfilePicURL, LikeCount, UserRecord); // Notify listeners
    }

    UFUNCTION(BlueprintCallable, Category = "TikTokPlugin|Events")
    void TriggerFollowEvent(
        const FString& Username,
        const FString& UserID,
        const FString& UserProfilePicURL,
        bool AlreadyFollowed,
        const FTikTokUserRecord& UserRecord)
    {
        OnFollowEvent.Broadcast(Username, UserID, UserProfilePicURL, AlreadyFollowed, UserRecord); // Notify listeners
    }

    UFUNCTION(BlueprintCallable, Category = "TikTokPlugin|Events")
    void TriggerShareEvent(
        const FString& Username,
        const FString& UserID,
        const FString& UserProfilePicURL,
        int32 Shares,
        const FTikTokUserRecord& UserRecord)
    {
        OnShareEvent.Broadcast(Username, UserID, UserProfilePicURL, Shares, UserRecord); // Notify listeners
    }

    UFUNCTION(BlueprintCallable, Category = "TikTokPlugin|Events")
    void TriggerRoomInfoEvent(const FString& RoomTitle, int32 ViewerCount)
    {
        OnRoomInfoEvent.Broadcast(RoomTitle, ViewerCount); // Notify listeners
    }

    UFUNCTION(BlueprintCallable, Category = "TikTokPlugin|Events")
    void TriggerJoinEvent(
        const FString& Username,
        const FString& UserID,
        const FString& UserProfilePicURL,
        const FTikTokUserRecord& UserRecord)
    {
        OnJoinEvent.Broadcast(Username, UserID, UserProfilePicURL, UserRecord); // Notify listeners
    }

    // connection lifecycle
    UFUNCTION(BlueprintCallable, Category = "TikTokPlugin|Connection")
    void TriggerDisconnectEvent(const FString& Reason)
    {
        OnDisconnected.Broadcast(Reason);  // Notify listeners of a disconnect with reason
    }

    UFUNCTION(BlueprintCallable, Category = "TikTokPlugin|Connection")
    void TriggerReconnectEvent()
    {
        OnReconnected.Broadcast();         // Notify listeners after a successful auto-reconnect
    }
};
