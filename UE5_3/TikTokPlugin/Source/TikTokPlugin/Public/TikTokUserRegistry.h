// TikTokUserRegistry.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Engine/Texture2DDynamic.h"
#include "TikTokTeams.h"            // Teams enums
#include "TikTokUserRegistry.generated.h"

// ---------------- User Record ----------------
USTRUCT(BlueprintType)
struct FTikTokUserRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "TikTok|Users")
    FString UserID;

    UPROPERTY(BlueprintReadOnly, Category = "TikTok|Users")
    FString Username;

    UPROPERTY(BlueprintReadOnly, Category = "TikTok|Users")
    FString AvatarURL;

    UPROPERTY(BlueprintReadOnly, Category = "TikTok|Users")
    bool bFollowed = false;

    UPROPERTY(BlueprintReadOnly, Category = "TikTok|Users")
    int32 TotalLikes = 0;

    UPROPERTY(BlueprintReadOnly, Category = "TikTok|Users")
    int32 TotalGiftValueDiamonds = 0;

    UPROPERTY(BlueprintReadOnly, Category = "TikTok|Users")
    int32 TotalShares = 0;

    UPROPERTY(BlueprintReadOnly, Category = "TikTok|Users")
    TObjectPtr<UTexture2DDynamic> ProfileTexture = nullptr;

    // Team membership (respects TeamMode)
    UPROPERTY(BlueprintReadOnly, Category = "TikTok|Users")
    ETikTokTeam Team = ETikTokTeam::None;
};

// ---------------- Leaderboard Types ----------------
UENUM(BlueprintType)
enum class ETikTokLeaderboardChanged : uint8
{
    Likes  UMETA(DisplayName = "Likes"),
    Gifts  UMETA(DisplayName = "Gifts"),
    Both   UMETA(DisplayName = "Both")
};

USTRUCT(BlueprintType)
struct FTikTokLeaderboardEntry
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "TikTok|Leaderboard")
    int32 Rank = 0;

    UPROPERTY(BlueprintReadOnly, Category = "TikTok|Leaderboard")
    FString UserID;

    // Convenience: full user record for UI binding
    UPROPERTY(BlueprintReadOnly, Category = "TikTok|Leaderboard")
    FTikTokUserRecord Record;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLeaderboardChanged, ETikTokLeaderboardChanged, Changed);

// Team leaderboard event (fires for a specific team; includes both lists)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnTeamLeaderboardChanged,
    ETikTokTeam, Team,
    const TArray<FTikTokLeaderboardEntry>&, TopLikes,
    const TArray<FTikTokLeaderboardEntry>&, TopGifts);

// ---------------- Registry ----------------
UCLASS(BlueprintType)
class UTikTokUserRegistry : public UObject
{
    GENERATED_BODY()

public:
    // ---------- Singleton Access ----------
    static UTikTokUserRegistry* Get();

    // wipes users and both leaderboards + team leaderboards
    UFUNCTION(BlueprintCallable, Category = "TikTok|Users")
    void ClearUsers(); 

    // zeroes likes + clears likes leaderboard + team likes LBs
    UFUNCTION(BlueprintCallable, Category = "TikTok|Users")
    void ClearLikes(); 

    // zeroes gifts + clears gifts leaderboard + team gifts LBs
    UFUNCTION(BlueprintCallable, Category = "TikTok|Users")
    void ClearGifts(); 

    // zeroes shares (no leaderboard)
    UFUNCTION(BlueprintCallable, Category = "TikTok|Users")
    void ClearShares(); 

    // option to set User Texture manually (if auto cache is disabled)
    UFUNCTION(BlueprintCallable, Category = "TikTok|Users")
    void SetUserProfileTexture(const FString& UserID, UTexture2DDynamic* Texture);

	// gets user texture (may be null)
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "TikTok|Users")
    UTexture2DDynamic* GetUserProfileTexture(const FString& UserID) const;

    // gets user record
    UFUNCTION(BlueprintCallable, Category = "TikTok|Users")
    bool GetUser(const FString& UserID, FTikTokUserRecord& OutRecord) const;

    // gets all user records
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "TikTok|Users")
    const TMap<FString, FTikTokUserRecord>& GetAllUsers() const { return Users; }

    // set user likes
    UFUNCTION(BlueprintCallable, Category = "TikTok|Users")
    bool SetUserLikes(const FString& UserID, int32 NewLikes);

	// set user gift diamonds
    UFUNCTION(BlueprintCallable, Category = "TikTok|Users")
    bool SetUserGiftDiamonds(const FString& UserID, int32 NewDiamonds);

    // resets user likes to zero
    UFUNCTION(BlueprintCallable, Category = "TikTok|Users")
    bool ResetUserLikes(const FString& UserID) { return SetUserLikes(UserID, 0); }

	// resets user gift diamonds to zero
    UFUNCTION(BlueprintCallable, Category = "TikTok|Users")
    bool ResetUserGiftDiamonds(const FString& UserID) { return SetUserGiftDiamonds(UserID, 0); }

    // reset all user stats in one call
    UFUNCTION(BlueprintCallable, Category = "TikTok|Users")
    bool ResetUserStats(const FString& UserID, bool bResetLikes = true, bool bResetGifts = true, bool bResetShares = false);

    // Auto-cache toggle 
    UFUNCTION(BlueprintCallable, Category = "TikTok|Users")
    void SetAutoCacheAvatars(bool bEnabled);

	// returns current auto-cache setting
    UFUNCTION(BlueprintPure, Category = "TikTok|Users")
    bool GetAutoCacheAvatars() const { return bAutoCacheAvatars; }

	// gets leaderboard top [n] likes (sorted, highest first)
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "TikTok|Leaderboard")
    void GetTopLikes(int32 MaxEntries, TArray<FTikTokLeaderboardEntry>& Out) const;

    // gets leaderboard top [n] gifters (sorted, highest first)
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "TikTok|Leaderboard")
    void GetTopGifts(int32 MaxEntries, TArray<FTikTokLeaderboardEntry>& Out) const;

    // Event: fires only when global leaderboard order/membership changes
    UPROPERTY(BlueprintAssignable, Category = "TikTok|Leaderboard")
    FOnLeaderboardChanged OnLeaderboardChanged;

    // sets team mode
    UFUNCTION(BlueprintCallable, Category = "TikTok|Teams")
    void SetTeamMode(ETikTokTeamMode InMode);

	// gets current team mode
    UFUNCTION(BlueprintPure, Category = "TikTok|Teams")
    ETikTokTeamMode GetTeamMode() const { return TeamMode; }

    // Set custom team join strings (users will join team by commenting the new strings)
    UFUNCTION(BlueprintCallable, Category = "TikTok|Teams")
    bool SetJoinTeamStrings(const FString& Team1Key, const FString& Team2Key, const FString& Team3Key, const FString& Team4Key);

    // Read back the current keys (raw values you set; may be empty for inactive teams)
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "TikTok|Teams")
    void GetJoinTeamStrings(FString& OutTeam1Key, FString& OutTeam2Key, FString& OutTeam3Key, FString& OutTeam4Key) const;

    // Assign a user's team (respects active TeamMode)
    UFUNCTION(BlueprintCallable, Category = "TikTok|Teams")
    bool SetUserTeam(const FString& UserID, ETikTokTeam NewTeam);

	// Get a user's current team (respects active TeamMode)
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "TikTok|Teams")
    bool GetUserTeam(const FString& UserID, ETikTokTeam& OutTeam) const;

    // Join-by-comment using current keys
    UFUNCTION(BlueprintCallable, Category = "TikTok|Teams")
    bool TryAssignTeamFromComment(const FString& UserID, const FString& CommentText);

	// returns total likes for a team
    UFUNCTION(BlueprintPure, Category = "TikTok|Teams")
    int32 GetTeamTotalLikes(ETikTokTeam Team) const;

	// returns total gift diamonds for a team
    UFUNCTION(BlueprintPure, Category = "TikTok|Teams")
    int32 GetTeamTotalGifts(ETikTokTeam Team) const;

    // get Team like leaderboards (sorted, highest first)
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "TikTok|Teams")
    void GetTeamTopLikes(ETikTokTeam Team, int32 MaxEntries, TArray<FTikTokLeaderboardEntry>& Out) const;

    // get Team gifter leaderboards (sorted, highest first)
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "TikTok|Teams")
    void GetTeamTopGifts(ETikTokTeam Team, int32 MaxEntries, TArray<FTikTokLeaderboardEntry>& Out) const;

    // Event: fires when a specific team's leaderboard structure changes (composition/order)
    UPROPERTY(BlueprintAssignable, Category = "TikTok|Teams")
    FOnTeamLeaderboardChanged OnTeamLeaderboardChanged;

	// when true, user likes and/or gifts reset to zero when they change teams
    UFUNCTION(BlueprintCallable, Category = "TikTok|Teams")
    void SetTeamAutoResetOnChange(bool bResetLikes, bool bResetGifts);

	// gets current auto-reset policy
    UFUNCTION(BlueprintPure, Category = "TikTok|Teams")
    void GetTeamAutoResetOnChange(bool& bOutResetLikes, bool& bOutResetGifts) const
    {
        bOutResetLikes = bAutoResetLikesOnTeamChange;
        bOutResetGifts = bAutoResetGiftsOnTeamChange;
    }

    // ---------- Parser-facing helpers (GameThread) ----------
    void AddOrUpdateUser(const FString& UserID, const FString& Username, const FString& AvatarURL);
    void AddLikes(const FString& UserID, int32 Delta);
    void AddGiftValue(const FString& UserID, int32 Diamonds);
    void AddShares(const FString& UserID, int32 Delta);
    void SetFollowed(const FString& UserID);

    // Called by the download proxy when a download finishes (intentionally public)
    void OnAvatarDownloaded(const FString& UserID, const FString& URL, UTexture2DDynamic* Texture, UObject* Proxy, bool bSuccess);

private:
    FTikTokUserRecord& EnsureUser(const FString& UserID);
    void EnsureAvatarAsync(const FString& UserID);

    // ---- Teams internals ----
    static constexpr int32 MaxTeams = 4;
    static int32      TeamToIndex(ETikTokTeam Team);
    static ETikTokTeam IndexToTeam(int32 Index);
    static int32      ModeToTeamCount(ETikTokTeamMode Mode);

    void RebuildAllTeamData(); // when mode changes

    static FString NormalizeKey(const FString& In);
    bool ValidateAndNormalizeJoinKeys(const TArray<FString>& InKeys, int32 ActiveTeamCount, TArray<FString>& OutNormalized, FString& OutErrorKey) const;

    // Per-team update helpers
    bool UpdateTopK_Likes_Team(int32 TeamIdx, const FString& UserID);
    bool UpdateTopK_Gifts_Team(int32 TeamIdx, const FString& UserID);
    bool RemoveFromTopByID_Team(int32 TeamIdx, const FString& UserID, bool bLikes, bool bGifts); // returns true if any list changed
    void BuildTeamTopLikesArray(int32 TeamIdx, TArray<FTikTokLeaderboardEntry>& Out, int32 ClampTo) const;
    void BuildTeamTopGiftsArray(int32 TeamIdx, TArray<FTikTokLeaderboardEntry>& Out, int32 ClampTo) const;
    void BroadcastTeamArraysIfChanged(int32 TeamIdx, bool bLikesChanged, bool bGiftsChanged);

    // ---- Global leaderboard internals ----
    static constexpr int32 MaxLeaderboardSize = 10;

    bool UpdateTopK_Likes(const FString& UserID);
    bool UpdateTopK_Gifts(const FString& UserID);
    bool RemoveFromTopLikes_Global(const FString& UserID);
    bool RemoveFromTopGifts_Global(const FString& UserID);

    void BuildTopArrayLikes(TArray<FTikTokLeaderboardEntry>& Out, int32 ClampTo) const;
    void BuildTopArrayGifts(TArray<FTikTokLeaderboardEntry>& Out, int32 ClampTo) const;

    bool LikesBetterThan(const FString& A, const FString& B) const; // tie-break
    bool GiftsBetterThan(const FString& A, const FString& B) const; // tie-break

    void BroadcastLeaderboards(ETikTokLeaderboardChanged Changed);

    // ---- Switches ----
    UPROPERTY()
    bool bAutoCacheAvatars = false;

    UPROPERTY()
    ETikTokTeamMode TeamMode = ETikTokTeamMode::None;

    // Team auto-reset policy
    UPROPERTY()
    bool bAutoResetLikesOnTeamChange = false;

    UPROPERTY()
    bool bAutoResetGiftsOnTeamChange = false;

private:
    UPROPERTY()
    TMap<FString, FTikTokUserRecord> Users;

    // Keep download proxies alive until completion (stored as UObject to avoid exposing proxy type)
    UPROPERTY()
    TArray<TObjectPtr<UObject>> ActiveAvatarDownloads;

    // De-dupe: "UserId|Url" keys that are currently being fetched
    TSet<FString> AvatarDownloadsInFlight;

    // ---- Global leaderboards (IDs only) ----
    TArray<FString> TopLikesIDs;
    TMap<FString, int32> IndexByID_Likes;

    TArray<FString> TopGiftsIDs;
    TMap<FString, int32> IndexByID_Gifts;

    // ---- Teams data ----
    // Join strings for Team1..Team4 (trimmed/lowercased for matching).
    // Stored normalized internally; defaults are "1","2","3","4".
    UPROPERTY()
    TArray<FString> TeamJoinKeys;

    // Running totals per team
    int32 TeamTotalLikes[MaxTeams] = {};
    int32 TeamTotalGifts[MaxTeams] = {};

    // Per-team leaderboards
    TArray<FString> TopLikesIDsByTeam[MaxTeams];
    TMap<FString, int32> IndexByID_LikesByTeam[MaxTeams];

    TArray<FString> TopGiftsIDsByTeam[MaxTeams];
    TMap<FString, int32> IndexByID_GiftsByTeam[MaxTeams];
};
