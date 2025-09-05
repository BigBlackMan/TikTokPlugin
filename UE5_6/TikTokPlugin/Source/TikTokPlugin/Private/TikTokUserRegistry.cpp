// TikTokUserRegistry.cpp

#include "TikTokUserRegistry.h"
#include "Engine/Engine.h"
#include "TikTokAvatarDownloadProxy.h"

DEFINE_LOG_CATEGORY_STATIC(LogTikTokRegistry, Log, All);

// Singleton instance (module-lifetime)
static UTikTokUserRegistry* GTikTokUserRegistry = nullptr;

UTikTokUserRegistry* UTikTokUserRegistry::Get()
{
    if (!GTikTokUserRegistry)
    {
        GTikTokUserRegistry = NewObject<UTikTokUserRegistry>();
        if (GTikTokUserRegistry)
        {
            GTikTokUserRegistry->AddToRoot();

            // Default join keys "1","2","3","4" (normalized)
            if (GTikTokUserRegistry->TeamJoinKeys.Num() == 0)
            {
                GTikTokUserRegistry->TeamJoinKeys.SetNum(MaxTeams);
                GTikTokUserRegistry->TeamJoinKeys[0] = TEXT("1");
                GTikTokUserRegistry->TeamJoinKeys[1] = TEXT("2");
                GTikTokUserRegistry->TeamJoinKeys[2] = TEXT("3");
                GTikTokUserRegistry->TeamJoinKeys[3] = TEXT("4");
                for (FString& K : GTikTokUserRegistry->TeamJoinKeys) { K = NormalizeKey(K); }
            }
        }
    }
    return GTikTokUserRegistry;
}

// ---------------- Blueprint API ----------------

void UTikTokUserRegistry::ClearUsers()
{
    const bool bHadLikes = TopLikesIDs.Num() > 0;
    const bool bHadGifts = TopGiftsIDs.Num() > 0;

    Users.Empty();
    TopLikesIDs.Empty(); IndexByID_Likes.Empty();
    TopGiftsIDs.Empty(); IndexByID_Gifts.Empty();

    // Teams: clear per-team structures & totals
    for (int32 t = 0; t < MaxTeams; ++t)
    {
        TopLikesIDsByTeam[t].Empty(); IndexByID_LikesByTeam[t].Empty();
        TopGiftsIDsByTeam[t].Empty(); IndexByID_GiftsByTeam[t].Empty();
        TeamTotalLikes[t] = 0; TeamTotalGifts[t] = 0;
    }

    // (We do not cancel in-flight avatar downloads. Completion will no-op.)

    if (bHadLikes && bHadGifts) { BroadcastLeaderboards(ETikTokLeaderboardChanged::Both); }
    else if (bHadLikes) { BroadcastLeaderboards(ETikTokLeaderboardChanged::Likes); }
    else if (bHadGifts) { BroadcastLeaderboards(ETikTokLeaderboardChanged::Gifts); }

    // Emit empty snapshots for teams
    for (int32 t = 0; t < MaxTeams; ++t)
    {
        TArray<FTikTokLeaderboardEntry> EmptyA, EmptyB;
        OnTeamLeaderboardChanged.Broadcast(IndexToTeam(t), EmptyA, EmptyB);
    }
}

void UTikTokUserRegistry::ClearLikes()
{
    const bool bAnyChange = TopLikesIDs.Num() > 0;

    for (auto& Pair : Users) { Pair.Value.TotalLikes = 0; }

    // Global likes leaderboard
    TopLikesIDs.Empty();
    IndexByID_Likes.Empty();

    // Team totals & team likes leaderboards
    for (int32 t = 0; t < MaxTeams; ++t)
    {
        TeamTotalLikes[t] = 0;
        TopLikesIDsByTeam[t].Empty();
        IndexByID_LikesByTeam[t].Empty();

        // Re-accumulate from users that belong to this team
        for (const auto& Pair : Users)
        {
            const FTikTokUserRecord& Rec = Pair.Value;
            const int32 TeamIdx = TeamToIndex(Rec.Team);
            if (TeamIdx == t && TeamIdx >= 0 && TeamIdx < MaxTeams)
            {
                TeamTotalLikes[t] += Rec.TotalLikes;
                if (Rec.TotalLikes > 0) UpdateTopK_Likes_Team(TeamIdx, Pair.Key);
            }
        }

        // Notify per team
        TArray<FTikTokLeaderboardEntry> TL, TG;
        BuildTeamTopLikesArray(t, TL, MaxLeaderboardSize);
        BuildTeamTopGiftsArray(t, TG, MaxLeaderboardSize);
        OnTeamLeaderboardChanged.Broadcast(IndexToTeam(t), TL, TG);
    }

    if (bAnyChange) { BroadcastLeaderboards(ETikTokLeaderboardChanged::Likes); }
}

void UTikTokUserRegistry::ClearGifts()
{
    const bool bAnyChange = TopGiftsIDs.Num() > 0;

    for (auto& Pair : Users) { Pair.Value.TotalGiftValueDiamonds = 0; }

    TopGiftsIDs.Empty();
    IndexByID_Gifts.Empty();

    // Team totals & team gifts leaderboards
    for (int32 t = 0; t < MaxTeams; ++t)
    {
        TeamTotalGifts[t] = 0;
        TopGiftsIDsByTeam[t].Empty();
        IndexByID_GiftsByTeam[t].Empty();

        for (const auto& Pair : Users)
        {
            const FTikTokUserRecord& Rec = Pair.Value;
            const int32 TeamIdx = TeamToIndex(Rec.Team);
            if (TeamIdx == t && TeamIdx >= 0 && TeamIdx < MaxTeams)
            {
                TeamTotalGifts[t] += Rec.TotalGiftValueDiamonds;
                if (Rec.TotalGiftValueDiamonds > 0) UpdateTopK_Gifts_Team(TeamIdx, Pair.Key);
            }
        }

        // Notify per team
        TArray<FTikTokLeaderboardEntry> TL, TG;
        BuildTeamTopLikesArray(t, TL, MaxLeaderboardSize);
        BuildTeamTopGiftsArray(t, TG, MaxLeaderboardSize);
        OnTeamLeaderboardChanged.Broadcast(IndexToTeam(t), TL, TG);
    }

    if (bAnyChange) { BroadcastLeaderboards(ETikTokLeaderboardChanged::Gifts); }
}

void UTikTokUserRegistry::ClearShares()
{
    for (auto& Pair : Users) { Pair.Value.TotalShares = 0; }
}

void UTikTokUserRegistry::SetUserProfileTexture(const FString& UserID, UTexture2DDynamic* Texture)
{
    if (UserID.IsEmpty() || !Texture) return;
    FTikTokUserRecord& Rec = EnsureUser(UserID);
    Rec.ProfileTexture = Texture;
}

UTexture2DDynamic* UTikTokUserRegistry::GetUserProfileTexture(const FString& UserID) const
{
    if (const FTikTokUserRecord* Rec = Users.Find(UserID))
    {
        return Rec->ProfileTexture;
    }
    return nullptr;
}

bool UTikTokUserRegistry::GetUser(const FString& UserID, FTikTokUserRecord& OutRecord) const
{
    if (const FTikTokUserRecord* Rec = Users.Find(UserID))
    {
        OutRecord = *Rec;
        return true;
    }
    return false;
}

// ---- NEW: Per-user mutators ----
bool UTikTokUserRegistry::SetUserLikes(const FString& UserID, int32 NewLikes)
{
    if (UserID.IsEmpty()) return false;
    if (NewLikes < 0) NewLikes = 0;

    FTikTokUserRecord& Rec = EnsureUser(UserID);
    if (Rec.TotalLikes == NewLikes) return false;

    const int32 Old = Rec.TotalLikes;
    const int32 Delta = NewLikes - Old;
    Rec.TotalLikes = NewLikes;

    bool bGlobalChanged = false;

    if (Delta > 0)
    {
        bGlobalChanged |= UpdateTopK_Likes(UserID);
    }
    else
    {
        // Decrease: remove then try reinsert (if still qualifies)
        bGlobalChanged |= RemoveFromTopLikes_Global(UserID);
        bGlobalChanged |= UpdateTopK_Likes(UserID);
    }

    // Team adjustments
    const int32 TeamIdx = TeamToIndex(Rec.Team);
    if (TeamIdx >= 0)
    {
        TeamTotalLikes[TeamIdx] += Delta;
        bool bTeamChanged = false;
        if (Delta > 0)
        {
            bTeamChanged |= UpdateTopK_Likes_Team(TeamIdx, UserID);
        }
        else
        {
            bTeamChanged |= RemoveFromTopByID_Team(TeamIdx, UserID, /*Likes*/true, /*Gifts*/false);
            bTeamChanged |= UpdateTopK_Likes_Team(TeamIdx, UserID);
        }
        BroadcastTeamArraysIfChanged(TeamIdx, bTeamChanged, /*gifts*/false);
    }

    if (bGlobalChanged) BroadcastLeaderboards(ETikTokLeaderboardChanged::Likes);
    return true;
}

bool UTikTokUserRegistry::SetUserGiftDiamonds(const FString& UserID, int32 NewDiamonds)
{
    if (UserID.IsEmpty()) return false;
    if (NewDiamonds < 0) NewDiamonds = 0;

    FTikTokUserRecord& Rec = EnsureUser(UserID);
    if (Rec.TotalGiftValueDiamonds == NewDiamonds) return false;

    const int32 Old = Rec.TotalGiftValueDiamonds;
    const int32 Delta = NewDiamonds - Old;
    Rec.TotalGiftValueDiamonds = NewDiamonds;

    bool bGlobalChanged = false;

    if (Delta > 0)
    {
        bGlobalChanged |= UpdateTopK_Gifts(UserID);
    }
    else
    {
        bGlobalChanged |= RemoveFromTopGifts_Global(UserID);
        bGlobalChanged |= UpdateTopK_Gifts(UserID);
    }

    // Team adjustments
    const int32 TeamIdx = TeamToIndex(Rec.Team);
    if (TeamIdx >= 0)
    {
        TeamTotalGifts[TeamIdx] += Delta;
        bool bTeamChanged = false;
        if (Delta > 0)
        {
            bTeamChanged |= UpdateTopK_Gifts_Team(TeamIdx, UserID);
        }
        else
        {
            bTeamChanged |= RemoveFromTopByID_Team(TeamIdx, UserID, /*Likes*/false, /*Gifts*/true);
            bTeamChanged |= UpdateTopK_Gifts_Team(TeamIdx, UserID);
        }
        BroadcastTeamArraysIfChanged(TeamIdx, /*likes*/false, bTeamChanged);
    }

    if (bGlobalChanged) BroadcastLeaderboards(ETikTokLeaderboardChanged::Gifts);
    return true;
}

bool UTikTokUserRegistry::ResetUserStats(const FString& UserID, bool bResetLikes, bool bResetGifts, bool bResetShares)
{
    bool bAny = false;
    if (bResetLikes)  bAny |= SetUserLikes(UserID, 0);
    if (bResetGifts)  bAny |= SetUserGiftDiamonds(UserID, 0);
    if (bResetShares)
    {
        if (FTikTokUserRecord* Rec = Users.Find(UserID))
        {
            Rec->TotalShares = 0;
            bAny = true;
        }
    }
    return bAny;
}

// Leaderboard getters (global)
void UTikTokUserRegistry::GetTopLikes(int32 MaxEntries, TArray<FTikTokLeaderboardEntry>& Out) const
{
    BuildTopArrayLikes(Out, MaxEntries);
}
void UTikTokUserRegistry::GetTopGifts(int32 MaxEntries, TArray<FTikTokLeaderboardEntry>& Out) const
{
    BuildTopArrayGifts(Out, MaxEntries);
}

// Auto-cache toggle
void UTikTokUserRegistry::SetAutoCacheAvatars(bool bEnabled)
{
    if (bAutoCacheAvatars == bEnabled) return;

    bAutoCacheAvatars = bEnabled;

    // If enabling mid-session, backfill any users missing textures.
    if (bAutoCacheAvatars)
    {
        for (const auto& Pair : Users)
        {
            const FTikTokUserRecord& Rec = Pair.Value;
            if (!Rec.ProfileTexture && !Rec.AvatarURL.IsEmpty())
            {
                EnsureAvatarAsync(Pair.Key);
            }
        }
    }
}

// ---------------- Teams (Blueprint API) ----------------
void UTikTokUserRegistry::SetTeamMode(ETikTokTeamMode InMode)
{
    if (TeamMode == InMode) return;
    TeamMode = InMode;
    RebuildAllTeamData();
}

void UTikTokUserRegistry::SetTeamAutoResetOnChange(bool bResetLikes, bool bResetGifts)
{
    bAutoResetLikesOnTeamChange = bResetLikes;
    bAutoResetGiftsOnTeamChange = bResetGifts;
}

bool UTikTokUserRegistry::SetUserTeam(const FString& UserID, ETikTokTeam NewTeam)
{
    FTikTokUserRecord& Rec = EnsureUser(UserID);

    // Clamp against current TeamMode
    const int32 Count = ModeToTeamCount(TeamMode);
    ETikTokTeam Clamped = ETikTokTeam::None;
    switch (NewTeam)
    {
    case ETikTokTeam::Team1: if (Count >= 1) Clamped = ETikTokTeam::Team1; break;
    case ETikTokTeam::Team2: if (Count >= 2) Clamped = ETikTokTeam::Team2; break;
    case ETikTokTeam::Team3: if (Count >= 3) Clamped = ETikTokTeam::Team3; break;
    case ETikTokTeam::Team4: if (Count >= 4) Clamped = ETikTokTeam::Team4; break;
    default: break;
    }

    const int32 OldIdx = TeamToIndex(Rec.Team);
    const int32 NewIdx = TeamToIndex(Clamped);
    if (OldIdx == NewIdx) return false; // no change (includes both None)

    // Remove from old team totals/leaderboards
    bool bOldLikesChanged = false, bOldGiftsChanged = false;
    if (OldIdx >= 0)
    {
        TeamTotalLikes[OldIdx] -= Rec.TotalLikes;
        TeamTotalGifts[OldIdx] -= Rec.TotalGiftValueDiamonds;
        bOldLikesChanged = bOldGiftsChanged = RemoveFromTopByID_Team(OldIdx, UserID, /*Likes*/true, /*Gifts*/true);
    }

    // Optional auto reset on real team switch (only if moving to a valid team)
    bool bGlobalLikesChanged = false, bGlobalGiftsChanged = false;
    if (NewIdx >= 0)
    {
        if (bAutoResetLikesOnTeamChange && Rec.TotalLikes > 0)
        {
            bGlobalLikesChanged |= RemoveFromTopLikes_Global(UserID);
            Rec.TotalLikes = 0;
        }
        if (bAutoResetGiftsOnTeamChange && Rec.TotalGiftValueDiamonds > 0)
        {
            bGlobalGiftsChanged |= RemoveFromTopGifts_Global(UserID);
            Rec.TotalGiftValueDiamonds = 0;
        }
    }

    // Assign
    Rec.Team = Clamped;

    // Add to new team totals/leaderboards (post-reset values)
    bool bNewLikesChanged = false, bNewGiftsChanged = false;
    if (NewIdx >= 0)
    {
        TeamTotalLikes[NewIdx] += Rec.TotalLikes;
        TeamTotalGifts[NewIdx] += Rec.TotalGiftValueDiamonds;
        if (Rec.TotalLikes > 0)  bNewLikesChanged = UpdateTopK_Likes_Team(NewIdx, UserID);
        if (Rec.TotalGiftValueDiamonds > 0) bNewGiftsChanged = UpdateTopK_Gifts_Team(NewIdx, UserID);
    }

    // Broadcast team changes
    if (OldIdx >= 0) BroadcastTeamArraysIfChanged(OldIdx, bOldLikesChanged, bOldGiftsChanged);
    if (NewIdx >= 0) BroadcastTeamArraysIfChanged(NewIdx, bNewLikesChanged, bNewGiftsChanged);

    // Global broadcasts if auto reset touched global boards
    if (bGlobalLikesChanged && bGlobalGiftsChanged) BroadcastLeaderboards(ETikTokLeaderboardChanged::Both);
    else if (bGlobalLikesChanged) BroadcastLeaderboards(ETikTokLeaderboardChanged::Likes);
    else if (bGlobalGiftsChanged) BroadcastLeaderboards(ETikTokLeaderboardChanged::Gifts);

    return true;
}

bool UTikTokUserRegistry::GetUserTeam(const FString& UserID, ETikTokTeam& OutTeam) const
{
    if (const FTikTokUserRecord* Rec = Users.Find(UserID))
    {
        OutTeam = Rec->Team;
        return true;
    }
    OutTeam = ETikTokTeam::None;
    return false;
}

FString UTikTokUserRegistry::NormalizeKey(const FString& In)
{
    FString S = In;
    S.TrimStartAndEndInline();
    S.ToLowerInline();
    return S;
}

bool UTikTokUserRegistry::ValidateAndNormalizeJoinKeys(
    const TArray<FString>& InKeys,
    int32 ActiveTeamCount,
    TArray<FString>& OutNormalized,
    FString& OutErrorKey) const
{
    OutNormalized.Reset();
    OutNormalized.SetNum(MaxTeams);

    // Normalize first
    for (int32 i = 0; i < MaxTeams; ++i)
    {
        OutNormalized[i] = NormalizeKey((i < InKeys.Num()) ? InKeys[i] : FString());
    }

    // Validate only the first N keys based on current TeamMode
    TSet<FString> Seen;
    for (int32 i = 0; i < ActiveTeamCount; ++i)
    {
        const FString& Key = OutNormalized[i];
        if (Key.IsEmpty())
        {
            OutErrorKey = TEXT("<empty>");
            return false; // require non-empty for active teams
        }
        if (Seen.Contains(Key))
        {
            OutErrorKey = Key;
            return false; // duplicate among active teams
        }
        Seen.Add(Key);
    }
    return true;
}

bool UTikTokUserRegistry::SetJoinTeamStrings(const FString& Team1Key, const FString& Team2Key, const FString& Team3Key, const FString& Team4Key)
{
    const int32 Count = ModeToTeamCount(TeamMode);

    TArray<FString> Raw; Raw.Reserve(MaxTeams);
    Raw.Add(Team1Key); Raw.Add(Team2Key); Raw.Add(Team3Key); Raw.Add(Team4Key);

    TArray<FString> Normalized; FString Err;
    if (!ValidateAndNormalizeJoinKeys(Raw, Count, Normalized, Err))
    {
        UE_LOG(LogTikTokRegistry, Warning, TEXT("SetJoinTeamStrings failed (duplicate or invalid key: %s)."), *Err);
        return false;
    }

    TeamJoinKeys = MoveTemp(Normalized);
    return true;
}

void UTikTokUserRegistry::GetJoinTeamStrings(FString& OutTeam1Key, FString& OutTeam2Key, FString& OutTeam3Key, FString& OutTeam4Key) const
{
    auto GetOrEmpty = [this](int32 I)->FString { return (TeamJoinKeys.IsValidIndex(I) ? TeamJoinKeys[I] : FString()); };
    OutTeam1Key = GetOrEmpty(0);
    OutTeam2Key = GetOrEmpty(1);
    OutTeam3Key = GetOrEmpty(2);
    OutTeam4Key = GetOrEmpty(3);
}

bool UTikTokUserRegistry::TryAssignTeamFromComment(const FString& UserID, const FString& CommentText)
{
    const int32 Count = ModeToTeamCount(TeamMode);
    if (Count <= 0) return false;

    const FString S = NormalizeKey(CommentText);

    // Ensure defaults exist if not set yet
    if (TeamJoinKeys.Num() == 0)
    {
        const_cast<UTikTokUserRegistry*>(this)->TeamJoinKeys.SetNum(MaxTeams);
        TeamJoinKeys[0] = TEXT("1");
        TeamJoinKeys[1] = TEXT("2");
        TeamJoinKeys[2] = TEXT("3");
        TeamJoinKeys[3] = TEXT("4");
        for (FString& K : TeamJoinKeys) { K = NormalizeKey(K); }
    }

    for (int32 i = 0; i < Count; ++i)
    {
        if (S == TeamJoinKeys[i])
        {
            return SetUserTeam(UserID, IndexToTeam(i));
        }
    }
    return false;
}

int32 UTikTokUserRegistry::GetTeamTotalLikes(ETikTokTeam Team) const
{
    const int32 Idx = TeamToIndex(Team);
    return (Idx >= 0 && Idx < MaxTeams) ? TeamTotalLikes[Idx] : 0;
}

int32 UTikTokUserRegistry::GetTeamTotalGifts(ETikTokTeam Team) const
{
    const int32 Idx = TeamToIndex(Team);
    return (Idx >= 0 && Idx < MaxTeams) ? TeamTotalGifts[Idx] : 0;
}

void UTikTokUserRegistry::GetTeamTopLikes(ETikTokTeam Team, int32 MaxEntries, TArray<FTikTokLeaderboardEntry>& Out) const
{
    const int32 Idx = TeamToIndex(Team);
    if (Idx < 0) { Out.Reset(); return; }
    BuildTeamTopLikesArray(Idx, Out, MaxEntries);
}
void UTikTokUserRegistry::GetTeamTopGifts(ETikTokTeam Team, int32 MaxEntries, TArray<FTikTokLeaderboardEntry>& Out) const
{
    const int32 Idx = TeamToIndex(Team);
    if (Idx < 0) { Out.Reset(); return; }
    BuildTeamTopGiftsArray(Idx, Out, MaxEntries);
}

// ---------------- Parser-facing helpers ----------------
void UTikTokUserRegistry::AddOrUpdateUser(const FString& UserID, const FString& Username, const FString& AvatarURL)
{
    if (UserID.IsEmpty()) return;

    FTikTokUserRecord& Rec = EnsureUser(UserID);
    if (!Username.IsEmpty())  Rec.Username = Username;
    if (!AvatarURL.IsEmpty()) Rec.AvatarURL = AvatarURL;
    Rec.UserID = UserID;

    EnsureAvatarAsync(UserID);
}

void UTikTokUserRegistry::AddLikes(const FString& UserID, int32 Delta)
{
    if (Delta <= 0) return;
    FTikTokUserRecord& Rec = EnsureUser(UserID);
    Rec.TotalLikes += Delta;

    bool bGlobalChanged = UpdateTopK_Likes(UserID);
    if (bGlobalChanged)
    {
        BroadcastLeaderboards(ETikTokLeaderboardChanged::Likes);
    }

    // Team updates
    const int32 TeamIdx = TeamToIndex(Rec.Team);
    if (TeamIdx >= 0)
    {
        TeamTotalLikes[TeamIdx] += Delta;
        bool bTeamChanged = UpdateTopK_Likes_Team(TeamIdx, UserID);
        BroadcastTeamArraysIfChanged(TeamIdx, bTeamChanged, /*gifts*/false);
    }
}

void UTikTokUserRegistry::AddGiftValue(const FString& UserID, int32 Diamonds)
{
    if (Diamonds <= 0) return;
    FTikTokUserRecord& Rec = EnsureUser(UserID);
    Rec.TotalGiftValueDiamonds += Diamonds;

    bool bGlobalChanged = UpdateTopK_Gifts(UserID);
    if (bGlobalChanged)
    {
        BroadcastLeaderboards(ETikTokLeaderboardChanged::Gifts);
    }

    // Team updates
    const int32 TeamIdx = TeamToIndex(Rec.Team);
    if (TeamIdx >= 0)
    {
        TeamTotalGifts[TeamIdx] += Diamonds;
        bool bTeamChanged = UpdateTopK_Gifts_Team(TeamIdx, UserID);
        BroadcastTeamArraysIfChanged(TeamIdx, /*likes*/false, bTeamChanged);
    }
}

void UTikTokUserRegistry::AddShares(const FString& UserID, int32 Delta)
{
    if (Delta <= 0) return;
    FTikTokUserRecord& Rec = EnsureUser(UserID);
    Rec.TotalShares += Delta;
}

void UTikTokUserRegistry::SetFollowed(const FString& UserID)
{
    FTikTokUserRecord& Rec = EnsureUser(UserID);
    Rec.bFollowed = true;
}

// ---------------- Internals ----------------
FTikTokUserRecord& UTikTokUserRegistry::EnsureUser(const FString& UserID)
{
    if (FTikTokUserRecord* Found = Users.Find(UserID)) return *Found;
    FTikTokUserRecord& NewRec = Users.Add(UserID);
    NewRec.UserID = UserID;
    return NewRec;
}

// ---- Avatar downloads ----
void UTikTokUserRegistry::EnsureAvatarAsync(const FString& UserID)
{
    if (!bAutoCacheAvatars) return;

    FTikTokUserRecord* Rec = Users.Find(UserID);
    if (!Rec) return;

    if (Rec->ProfileTexture) return;
    if (Rec->AvatarURL.IsEmpty()) return;

    const FString Key = UserID + TEXT("|") + Rec->AvatarURL;
    if (AvatarDownloadsInFlight.Contains(Key)) return;
    AvatarDownloadsInFlight.Add(Key);

    UTikTokAvatarDownloadProxy* Proxy = NewObject<UTikTokAvatarDownloadProxy>(this);
    ActiveAvatarDownloads.Add(Proxy);
    Proxy->Begin(this, UserID, Rec->AvatarURL);
}

void UTikTokUserRegistry::OnAvatarDownloaded(const FString& UserID, const FString& URL, UTexture2DDynamic* Texture, UObject* Proxy, bool bSuccess)
{
    const FString Key = UserID + TEXT("|") + URL;
    AvatarDownloadsInFlight.Remove(Key);

    if (FTikTokUserRecord* Rec = Users.Find(UserID))
    {
        if (!Rec->ProfileTexture && Rec->AvatarURL == URL && bSuccess && Texture)
        {
            Rec->ProfileTexture = Texture;
        }
    }

    ActiveAvatarDownloads.Remove(Proxy); // allow GC of the proxy+task
}

// ---- Global leaderboard helpers ----
bool UTikTokUserRegistry::LikesBetterThan(const FString& A, const FString& B) const
{
    const FTikTokUserRecord* RA = Users.Find(A);
    const FTikTokUserRecord* RB = Users.Find(B);
    const int32 TA = RA ? RA->TotalLikes : 0;
    const int32 TB = RB ? RB->TotalLikes : 0;

    if (TA != TB) return TA > TB;
    return A < B; // stable tie-break by UserID
}

bool UTikTokUserRegistry::GiftsBetterThan(const FString& A, const FString& B) const
{
    const FTikTokUserRecord* RA = Users.Find(A);
    const FTikTokUserRecord* RB = Users.Find(B);
    const int32 TA = RA ? RA->TotalGiftValueDiamonds : 0;
    const int32 TB = RB ? RB->TotalGiftValueDiamonds : 0;

    if (TA != TB) return TA > TB;
    return A < B;
}

bool UTikTokUserRegistry::RemoveFromTopLikes_Global(const FString& UserID)
{
    if (int32* FoundIdxPtr = IndexByID_Likes.Find(UserID))
    {
        const int32 I = *FoundIdxPtr;
        TopLikesIDs.RemoveAt(I);
        IndexByID_Likes.Remove(UserID);
        for (int32 k = I; k < TopLikesIDs.Num(); ++k)
        {
            IndexByID_Likes[TopLikesIDs[k]] = k;
        }
        return true;
    }
    return false;
}

bool UTikTokUserRegistry::RemoveFromTopGifts_Global(const FString& UserID)
{
    if (int32* FoundIdxPtr = IndexByID_Gifts.Find(UserID))
    {
        const int32 I = *FoundIdxPtr;
        TopGiftsIDs.RemoveAt(I);
        IndexByID_Gifts.Remove(UserID);
        for (int32 k = I; k < TopGiftsIDs.Num(); ++k)
        {
            IndexByID_Gifts[TopGiftsIDs[k]] = k;
        }
        return true;
    }
    return false;
}

bool UTikTokUserRegistry::UpdateTopK_Likes(const FString& UserID)
{
    if (!Users.Contains(UserID)) return false;
    bool bChanged = false;

    if (int32* FoundIdxPtr = IndexByID_Likes.Find(UserID))
    {
        int32 I = *FoundIdxPtr;
        while (I > 0 && LikesBetterThan(UserID, TopLikesIDs[I - 1]))
        {
            Swap(TopLikesIDs[I], TopLikesIDs[I - 1]);
            IndexByID_Likes[TopLikesIDs[I]] = I;
            IndexByID_Likes[TopLikesIDs[I - 1]] = I - 1;
            --I; bChanged = true;
        }
        return bChanged;
    }

    if (TopLikesIDs.Num() < MaxLeaderboardSize)
    {
        int32 InsertAt = 0;
        while (InsertAt < TopLikesIDs.Num() && LikesBetterThan(TopLikesIDs[InsertAt], UserID)) ++InsertAt;
        TopLikesIDs.Insert(UserID, InsertAt);
        for (int32 i = InsertAt; i < TopLikesIDs.Num(); ++i) IndexByID_Likes.FindOrAdd(TopLikesIDs[i]) = i;
        return true;
    }
    else
    {
        const FString& LastID = TopLikesIDs.Last();
        if (!LikesBetterThan(LastID, UserID))
        {
            int32 InsertAt = 0;
            while (InsertAt < TopLikesIDs.Num() && LikesBetterThan(TopLikesIDs[InsertAt], UserID)) ++InsertAt;
            TopLikesIDs.Insert(UserID, InsertAt);
            for (int32 i = InsertAt; i < TopLikesIDs.Num(); ++i) IndexByID_Likes.FindOrAdd(TopLikesIDs[i]) = i;
            const FString Evicted = TopLikesIDs.Pop();
            IndexByID_Likes.Remove(Evicted);
            return true;
        }
    }
    return false;
}

bool UTikTokUserRegistry::UpdateTopK_Gifts(const FString& UserID)
{
    if (!Users.Contains(UserID)) return false;
    bool bChanged = false;

    if (int32* FoundIdxPtr = IndexByID_Gifts.Find(UserID))
    {
        int32 I = *FoundIdxPtr;
        while (I > 0 && GiftsBetterThan(UserID, TopGiftsIDs[I - 1]))
        {
            Swap(TopGiftsIDs[I], TopGiftsIDs[I - 1]);
            IndexByID_Gifts[TopGiftsIDs[I]] = I;
            IndexByID_Gifts[TopGiftsIDs[I - 1]] = I - 1;
            --I; bChanged = true;
        }
        return bChanged;
    }

    if (TopGiftsIDs.Num() < MaxLeaderboardSize)
    {
        int32 InsertAt = 0;
        while (InsertAt < TopGiftsIDs.Num() && GiftsBetterThan(TopGiftsIDs[InsertAt], UserID)) ++InsertAt;
        TopGiftsIDs.Insert(UserID, InsertAt);
        for (int32 i = InsertAt; i < TopGiftsIDs.Num(); ++i) IndexByID_Gifts.FindOrAdd(TopGiftsIDs[i]) = i;
        return true;
    }
    else
    {
        const FString& LastID = TopGiftsIDs.Last();
        if (!GiftsBetterThan(LastID, UserID))
        {
            int32 InsertAt = 0;
            while (InsertAt < TopGiftsIDs.Num() && GiftsBetterThan(TopGiftsIDs[InsertAt], UserID)) ++InsertAt;
            TopGiftsIDs.Insert(UserID, InsertAt);
            for (int32 i = InsertAt; i < TopGiftsIDs.Num(); ++i) IndexByID_Gifts.FindOrAdd(TopGiftsIDs[i]) = i;
            const FString Evicted = TopGiftsIDs.Pop();
            IndexByID_Gifts.Remove(Evicted);
            return true;
        }
    }
    return false;
}

void UTikTokUserRegistry::BuildTopArrayLikes(TArray<FTikTokLeaderboardEntry>& Out, int32 ClampTo) const
{
    Out.Reset();
    const int32 N = ClampTo > 0 ? FMath::Min(ClampTo, TopLikesIDs.Num()) : TopLikesIDs.Num();
    Out.Reserve(N);
    for (int32 i = 0; i < N; ++i)
    {
        const FString& ID = TopLikesIDs[i];
        const FTikTokUserRecord* Rec = Users.Find(ID);
        if (!Rec) continue;

        FTikTokLeaderboardEntry E;
        E.Rank = i + 1;
        E.UserID = ID;
        E.Record = *Rec;
        Out.Add(MoveTemp(E));
    }
}

void UTikTokUserRegistry::BuildTopArrayGifts(TArray<FTikTokLeaderboardEntry>& Out, int32 ClampTo) const
{
    Out.Reset();
    const int32 N = ClampTo > 0 ? FMath::Min(ClampTo, TopGiftsIDs.Num()) : TopGiftsIDs.Num();
    Out.Reserve(N);
    for (int32 i = 0; i < N; ++i)
    {
        const FString& ID = TopGiftsIDs[i];
        const FTikTokUserRecord* Rec = Users.Find(ID);
        if (!Rec) continue;

        FTikTokLeaderboardEntry E;
        E.Rank = i + 1;
        E.UserID = ID;
        E.Record = *Rec;
        Out.Add(MoveTemp(E));
    }
}

void UTikTokUserRegistry::BroadcastLeaderboards(ETikTokLeaderboardChanged Changed)
{
    OnLeaderboardChanged.Broadcast(Changed);
}

// ---- Teams internals ----
int32 UTikTokUserRegistry::TeamToIndex(ETikTokTeam Team)
{
    switch (Team)
    {
    case ETikTokTeam::Team1: return 0;
    case ETikTokTeam::Team2: return 1;
    case ETikTokTeam::Team3: return 2;
    case ETikTokTeam::Team4: return 3;
    default: return -1;
    }
}
ETikTokTeam UTikTokUserRegistry::IndexToTeam(int32 Index)
{
    switch (Index)
    {
    case 0: return ETikTokTeam::Team1;
    case 1: return ETikTokTeam::Team2;
    case 2: return ETikTokTeam::Team3;
    case 3: return ETikTokTeam::Team4;
    default: return ETikTokTeam::None;
    }
}
int32 UTikTokUserRegistry::ModeToTeamCount(ETikTokTeamMode Mode)
{
    switch (Mode)
    {
    case ETikTokTeamMode::TwoTeams:   return 2;
    case ETikTokTeamMode::ThreeTeams: return 3;
    case ETikTokTeamMode::FourTeams:  return 4;
    default: return 0;
    }
}

void UTikTokUserRegistry::RebuildAllTeamData()
{
    // Reset all
    for (int32 t = 0; t < MaxTeams; ++t)
    {
        TopLikesIDsByTeam[t].Empty(); IndexByID_LikesByTeam[t].Empty();
        TopGiftsIDsByTeam[t].Empty(); IndexByID_GiftsByTeam[t].Empty();
        TeamTotalLikes[t] = 0; TeamTotalGifts[t] = 0;
    }

    const int32 Count = ModeToTeamCount(TeamMode);

    // Re-clamp user teams, accumulate totals, and rebuild top-Ks
    for (auto& Pair : Users)
    {
        FTikTokUserRecord& Rec = Pair.Value;

        // Clamp membership to current mode
        if (TeamToIndex(Rec.Team) >= Count)
        {
            Rec.Team = ETikTokTeam::None;
            continue;
        }

        const int32 TeamIdx = TeamToIndex(Rec.Team);
        if (TeamIdx < 0) continue;

        TeamTotalLikes[TeamIdx] += Rec.TotalLikes;
        TeamTotalGifts[TeamIdx] += Rec.TotalGiftValueDiamonds;

        if (Rec.TotalLikes > 0)  UpdateTopK_Likes_Team(TeamIdx, Pair.Key);
        if (Rec.TotalGiftValueDiamonds > 0) UpdateTopK_Gifts_Team(TeamIdx, Pair.Key);
    }

    // Emit per-team snapshots
    for (int32 t = 0; t < MaxTeams; ++t)
    {
        TArray<FTikTokLeaderboardEntry> TL, TG;
        BuildTeamTopLikesArray(t, TL, MaxLeaderboardSize);
        BuildTeamTopGiftsArray(t, TG, MaxLeaderboardSize);
        OnTeamLeaderboardChanged.Broadcast(IndexToTeam(t), TL, TG);
    }
}

bool UTikTokUserRegistry::UpdateTopK_Likes_Team(int32 TeamIdx, const FString& UserID)
{
    if (!Users.Contains(UserID)) return false;

    auto& IDs = TopLikesIDsByTeam[TeamIdx];
    auto& Map = IndexByID_LikesByTeam[TeamIdx];

    auto Better = [this](const FString& A, const FString& B)
        {
            return LikesBetterThan(A, B);
        };

    if (int32* FoundIdxPtr = Map.Find(UserID))
    {
        bool bChanged = false;
        int32 I = *FoundIdxPtr;
        while (I > 0 && Better(UserID, IDs[I - 1]))
        {
            Swap(IDs[I], IDs[I - 1]);
            Map[IDs[I]] = I;
            Map[IDs[I - 1]] = I - 1;
            --I; bChanged = true;
        }
        return bChanged;
    }

    if (IDs.Num() < MaxLeaderboardSize)
    {
        int32 InsertAt = 0;
        while (InsertAt < IDs.Num() && Better(IDs[InsertAt], UserID)) ++InsertAt;
        IDs.Insert(UserID, InsertAt);
        for (int32 i = InsertAt; i < IDs.Num(); ++i) Map.FindOrAdd(IDs[i]) = i;
        return true;
    }
    else
    {
        const FString& LastID = IDs.Last();
        if (!Better(LastID, UserID))
        {
            int32 InsertAt = 0;
            while (InsertAt < IDs.Num() && Better(IDs[InsertAt], UserID)) ++InsertAt;
            IDs.Insert(UserID, InsertAt);
            for (int32 i = InsertAt; i < IDs.Num(); ++i) Map.FindOrAdd(IDs[i]) = i;
            const FString Evicted = IDs.Pop();
            Map.Remove(Evicted);
            return true;
        }
    }
    return false;
}

bool UTikTokUserRegistry::UpdateTopK_Gifts_Team(int32 TeamIdx, const FString& UserID)
{
    if (!Users.Contains(UserID)) return false;

    auto& IDs = TopGiftsIDsByTeam[TeamIdx];
    auto& Map = IndexByID_GiftsByTeam[TeamIdx];

    auto Better = [this](const FString& A, const FString& B)
        {
            return GiftsBetterThan(A, B);
        };

    if (int32* FoundIdxPtr = Map.Find(UserID))
    {
        bool bChanged = false;
        int32 I = *FoundIdxPtr;
        while (I > 0 && Better(UserID, IDs[I - 1]))
        {
            Swap(IDs[I], IDs[I - 1]);
            Map[IDs[I]] = I;
            Map[IDs[I - 1]] = I - 1;
            --I; bChanged = true;
        }
        return bChanged;
    }

    if (IDs.Num() < MaxLeaderboardSize)
    {
        int32 InsertAt = 0;
        while (InsertAt < IDs.Num() && Better(IDs[InsertAt], UserID)) ++InsertAt;
        IDs.Insert(UserID, InsertAt);
        for (int32 i = InsertAt; i < IDs.Num(); ++i) Map.FindOrAdd(IDs[i]) = i;
        return true;
    }
    else
    {
        const FString& LastID = IDs.Last();
        if (!Better(LastID, UserID))
        {
            int32 InsertAt = 0;
            while (InsertAt < IDs.Num() && Better(IDs[InsertAt], UserID)) ++InsertAt;
            IDs.Insert(UserID, InsertAt);
            for (int32 i = InsertAt; i < IDs.Num(); ++i) Map.FindOrAdd(IDs[i]) = i;
            const FString Evicted = IDs.Pop();
            Map.Remove(Evicted);
            return true;
        }
    }
    return false;
}

bool UTikTokUserRegistry::RemoveFromTopByID_Team(int32 TeamIdx, const FString& UserID, bool bLikes, bool bGifts)
{
    bool bChanged = false;

    if (bLikes)
    {
        auto& IDs = TopLikesIDsByTeam[TeamIdx];
        auto& Map = IndexByID_LikesByTeam[TeamIdx];
        if (int32* FoundIdxPtr = Map.Find(UserID))
        {
            const int32 I = *FoundIdxPtr;
            IDs.RemoveAt(I);
            Map.Remove(UserID);
            for (int32 k = I; k < IDs.Num(); ++k) Map[IDs[k]] = k;
            bChanged = true;
        }
    }
    if (bGifts)
    {
        auto& IDs = TopGiftsIDsByTeam[TeamIdx];
        auto& Map = IndexByID_GiftsByTeam[TeamIdx];
        if (int32* FoundIdxPtr = Map.Find(UserID))
        {
            const int32 I = *FoundIdxPtr;
            IDs.RemoveAt(I);
            Map.Remove(UserID);
            for (int32 k = I; k < IDs.Num(); ++k) Map[IDs[k]] = k;
            bChanged = true;
        }
    }

    return bChanged;
}

void UTikTokUserRegistry::BuildTeamTopLikesArray(int32 TeamIdx, TArray<FTikTokLeaderboardEntry>& Out, int32 ClampTo) const
{
    Out.Reset();
    const TArray<FString>& IDs = TopLikesIDsByTeam[TeamIdx];
    const int32 N = ClampTo > 0 ? FMath::Min(ClampTo, IDs.Num()) : IDs.Num();
    Out.Reserve(N);

    for (int32 i = 0; i < N; ++i)
    {
        const FString& ID = IDs[i];
        const FTikTokUserRecord* Rec = Users.Find(ID);
        if (!Rec) continue;

        FTikTokLeaderboardEntry E;
        E.Rank = i + 1;
        E.UserID = ID;
        E.Record = *Rec;
        Out.Add(MoveTemp(E));
    }
}

void UTikTokUserRegistry::BuildTeamTopGiftsArray(int32 TeamIdx, TArray<FTikTokLeaderboardEntry>& Out, int32 ClampTo) const
{
    Out.Reset();
    const TArray<FString>& IDs = TopGiftsIDsByTeam[TeamIdx];
    const int32 N = ClampTo > 0 ? FMath::Min(ClampTo, IDs.Num()) : IDs.Num();
    Out.Reserve(N);

    for (int32 i = 0; i < N; ++i)
    {
        const FString& ID = IDs[i];
        const FTikTokUserRecord* Rec = Users.Find(ID);
        if (!Rec) continue;

        FTikTokLeaderboardEntry E;
        E.Rank = i + 1;
        E.UserID = ID;
        E.Record = *Rec;
        Out.Add(MoveTemp(E));
    }
}

void UTikTokUserRegistry::BroadcastTeamArraysIfChanged(int32 TeamIdx, bool bLikesChanged, bool bGiftsChanged)
{
    if (!bLikesChanged && !bGiftsChanged) return;

    TArray<FTikTokLeaderboardEntry> TL, TG;
    BuildTeamTopLikesArray(TeamIdx, TL, MaxLeaderboardSize);
    BuildTeamTopGiftsArray(TeamIdx, TG, MaxLeaderboardSize);
    OnTeamLeaderboardChanged.Broadcast(IndexToTeam(TeamIdx), TL, TG);
}
