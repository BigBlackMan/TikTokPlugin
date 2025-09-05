// Copyright Epic Games, Inc. All Rights Reserved.
// TikTokPluginBPLibrary.cpp — keep all Boost/OpenSSL code in this TU to avoid unity pollution

#include "TikTokPluginBPLibrary.h"                        // Header for this BP library
#include "TikTokUserRegistry.h"                           // User registry singleton
#include "CoreMinimal.h"                                  // Core UE types and logging
#include "HAL/Runnable.h"                                 // FRunnable base
#include "HAL/RunnableThread.h"                           // FRunnableThread
#include <atomic>                                         // std::atomic for stop flags
#include "HAL/PlatformProcess.h"                          // FPlatformProcess::Sleep for backoff

#include "TikTokPluginEventHandler.h"                     // Event handler for Blueprint delegate
#include "TikTokJsonParser.h"                             // Parser that fans events out on the Game Thread
#include "TikTokGiftQueueManager.h"                       // Gift queue manager (new)
#include "Async/Async.h" 

// --- Macro hygiene before 3rd-party headers (to avoid collisions) THIS NEEDS TO STAY HOW IT IS--- //
#ifdef check
# undef check                                              // Undefine Unreal's 'check' macro for Boost compatibility
# pragma message("✅ Unreal's check macro was undefined to allow Boost to compile.")
#endif
#ifdef UE_CHECK_IMPL
# undef UE_CHECK_IMPL                                     // Undefine UE_CHECK_IMPL macro for Boost compatibility
# pragma message("✅ Unreal's UE_CHECK_IMPL macro was undefined to allow Boost to compile.")
#endif
#ifdef F
# undef F                                                 // Undefine 'F' macro if present (rare) to avoid name clashes
# pragma message("✅ Macro F was undefined to allow Boost to compile.")
#endif

#define UI UI_ST                                          // Avoid UI macro conflicts in Windows headers
THIRD_PARTY_INCLUDES_START                                // Begin third-party include isolation
#include <openssl/ssl.h>                                  // OpenSSL TLS primitives
#include <boost/beast/core.hpp>                           // Beast core
#include <boost/beast/core/tcp_stream.hpp>                // Beast tcp_stream (timeout helpers live here)
#include <boost/beast/websocket.hpp>                      // Beast WebSocket
#include <boost/beast/ssl.hpp>                            // Beast SSL stream
#include <boost/asio/ssl.hpp>                             // Asio SSL context
#include <boost/asio/connect.hpp>                         // Asio connect helpers
THIRD_PARTY_INCLUDES_END                                  // End third-party include isolation
#undef UI                                                 // Restore UI macro alias

namespace beast = boost::beast;                           // Namespace alias for brevity
namespace websocket = beast::websocket;                   // Alias for websocket
namespace net = boost::asio;                              // Alias for Asio
namespace ssl = net::ssl;                                 // Alias for SSL
using tcp = net::ip::tcp;                                 // TCP resolver/socket alias

DEFINE_LOG_CATEGORY_STATIC(LogTikTokPlugin, Log, All);    // Plugin log category

// Connection constants (host/port remain fixed; API key now provided at runtime)
static const char* kEulerStreamHost = "ws.eulerstream.com"; // EulerStream WebSocket host
static const char* kEulerStreamPort = "443";                 // TLS port

// Static event handler instance
UTikTokPluginEventHandler* UTikTokPluginBPLibrary::StaticEventHandler = nullptr; // Global handler pointer


// ---------- Helpers to fire lifecycle events on the Game Thread ---------- //

static void BroadcastDisconnectedOnGameThread(const FString& Reason)             // Helper: broadcast OnDisconnected safely
{
    AsyncTask(ENamedThreads::GameThread, [Reason]()                               // Ensure we run on the Game Thread
        {
            if (UTikTokPluginEventHandler* EH = UTikTokPluginBPLibrary::GetTikTokEventHandler()) // Get event handler
            {
                EH->TriggerDisconnectEvent(Reason);                                   // Fire disconnect event
            }
            if (UTikTokGiftQueueManager* GM = UTikTokPluginBPLibrary::GetGiftQueueManager()) // Get gift queue manager
            {
                GM->HandleDisconnected();                                             // Let manager react (pause/retain/etc.)
            }
        });
}

static void BroadcastReconnectedOnGameThread()                                    // Helper: broadcast OnReconnected safely
{
    AsyncTask(ENamedThreads::GameThread, []()                                     // Ensure we run on the Game Thread
        {
            if (UTikTokPluginEventHandler* EH = UTikTokPluginBPLibrary::GetTikTokEventHandler()) // Get event handler
            {
                EH->TriggerReconnectEvent();                                          // Fire reconnect event
            }
            if (UTikTokGiftQueueManager* GM = UTikTokPluginBPLibrary::GetGiftQueueManager()) // Get gift queue manager
            {
                GM->HandleReconnected();                                              // Let manager react (resume/etc.)
            }
        });
}

// ---------- WebSocket thread worker with auto-reconnect ---------- //
class FTikTokWSRunnable : public FRunnable
{
public:
    explicit FTikTokWSRunnable(const FString& InHandle, const FString& InApiKey)  // Constructor: capture handle and API key
        : Handle(InHandle)                                                        // Save TikTok handle
        , ApiKey(InApiKey)                                                        // Save API key
    {
    }

    virtual ~FTikTokWSRunnable() {}                                               // Default destructor

    virtual bool Init() override { return true; }                                 // No special init required

    virtual uint32 Run() override                                                 // Thread entry point
    {
        // Sanitize inputs once (immutable across reconnects)
        FString CleanHandle = Handle;                                             // Copy the handle to sanitize
        CleanHandle.TrimStartAndEndInline();                                      // Trim whitespace
        CleanHandle.ReplaceInline(TEXT("@"), TEXT(""));                           // Remove leading '@' if present

        if (CleanHandle.IsEmpty())                                                // Validate handle
        {
            UE_LOG(LogTikTokPlugin, Error, TEXT("No TikTok handle provided. Connection aborted.")); // Log error
            return 0;                                                             // Exit thread
        }
        if (ApiKey.IsEmpty())                                                     // Validate API key
        {
            UE_LOG(LogTikTokPlugin, Error, TEXT("No API key provided. Visit Eulerstream.com for free API key -- Connection aborted."));       // Log error
            return 0;                                                             // Exit thread
        }

        const std::string host = kEulerStreamHost;                                // Host as std::string for Beast
        const std::string port = kEulerStreamPort;                                // Port as std::string for Beast
        const std::string uniqueId = TCHAR_TO_UTF8(*CleanHandle);                 // UTF-8 handle
        const std::string apiKey = TCHAR_TO_UTF8(*ApiKey);                      // UTF-8 API key

        bool bConnected = false;                                                  // Tracks current connection state
        bool bEverConnected = false;                                              // True after first successful connect
        int32 ReconnectAttempt = 0;                                               // Number of consecutive failed reconnects

        while (!bStopRequested)                                                   // Outer loop: connect/reconnect attempts
        {
            try
            {
                // --- Create connection-scoped I/O and SSL contexts --- //
                net::io_context ioc;                                              // Asio I/O context for this session
                ssl::context ctx(ssl::context::tls_client);                       // TLS v1.2+ client context

                // Keep verification relaxed by default (compatible with UE’s OpenSSL on Win)
                ctx.set_default_verify_paths();                                   // Load default CA paths (no hard fail)

                tcp::resolver resolver(ioc);                                      // DNS resolver
                auto const results = resolver.resolve(host, port);                 // Resolve host:port

                // Use beast::tcp_stream at the lowest layer so we can set timeouts
                using ssl_stream_t = beast::ssl_stream<beast::tcp_stream>;        // SSL over beast::tcp_stream
                websocket::stream<ssl_stream_t> ws(ioc, ctx);                      // WebSocket over SSL over beast::tcp_stream

                // SNI: set host name for the TLS handshake (helps virtual hosts)
                if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str())) // Set SNI for TLS
                {
                    beast::error_code ec{ static_cast<int>(::ERR_get_error()), net::error::get_ssl_category() }; // Extract SSL error
                    UE_LOG(LogTikTokPlugin, Error, TEXT("SSL Error: %s"), *FString(ec.message().c_str()));       // Log SSL error
                }

                // Connect TCP using the beast tcp_stream helper
                beast::get_lowest_layer(ws).connect(results);                      // TCP connect (lowest layer is tcp_stream)
                ws.next_layer().handshake(ssl::stream_base::client);              // TLS handshake

                // Build request target with query string (handle + API key)
                const std::string target = std::string("/?uniqueId=") + uniqueId + "&apiKey=" + apiKey; // WebSocket target path

                ws.handshake(host, target);                                       // WebSocket handshake

                // Successful connection reached here
                UE_LOG(LogTikTokPlugin, Log, TEXT("Connected to EulerStream for handle '%s'."), *CleanHandle); // Log connection
                if (bEverConnected && ReconnectAttempt > 0)                       // If this is a reconnect
                {
                    BroadcastReconnectedOnGameThread();                           // Fire OnReconnected on Game Thread
                }
                bConnected = true;                                                // Mark connected
                bEverConnected = true;                                            // Remember first connect occurred
                ReconnectAttempt = 0;                                             // Reset attempt counter

                // --- Read loop --- //
                beast::flat_buffer buffer;                                        // Incoming message buffer

                while (!bStopRequested)                                           // Inner loop: read frames
                {
                    // Set a finite read deadline so we can poll the stop flag periodically
                    beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(5)); // Timeout for read operation

                    boost::system::error_code ec;                                 // Error code for non-throwing read
                    buffer.clear();                                               // Clear/prepare buffer
                    ws.read(buffer, ec);                                          // Blocking read (with timeout)

                    if (ec)                                                       // If an error occurred
                    {
                        if (ec == beast::error::timeout)                          // Read timed out
                        {
                            continue;                                             // Keep the loop alive (heartbeat)
                        }
                        UE_LOG(LogTikTokPlugin, Warning, TEXT("WebSocket read error: %s"), *FString(ec.message().c_str())); // Log read error
                        break;                                                    // Break inner loop to trigger reconnect logic
                    }

                    // Convert buffer to UE string and dispatch to parser (Game Thread fan-out)
                    std::string Raw = beast::buffers_to_string(buffer.data());    // Convert buffer to std::string
                    const FString UEMessage = UTF8_TO_TCHAR(Raw.c_str());         // Convert to FString

                    FTikTokJsonParser::ParseAndDispatchAll(                       // Parse + dispatch Blueprint events
                        UEMessage,
                        UTikTokPluginBPLibrary::GetTikTokEventHandler()
                    );
                }

                // If exit due to stop, close gracefully; otherwise fall to reconnect path
                if (bStopRequested)
                {
                    boost::system::error_code ecClose;                            // Error code for close
                    ws.close(websocket::close_code::normal, ecClose);             // Attempt a clean close (ignore errors)
                    bConnected = false;                                           // No longer connected
                    break;                                                        // Exit the outer loop cleanly
                }
            }
            catch (const std::exception& e)                                       // Catch connect/handshake/read exceptions
            {
                UE_LOG(LogTikTokPlugin, Error, TEXT("WebSocket exception: %s"), *FString(e.what())); // Log exception details
            }

            // Unexpected disconnect or handshake/connect failure
            if (bStopRequested)                                                   // If a stop was requested, exit cleanly
            {
                break;                                                            // Leave outer loop
            }

            if (bConnected)                                                       // If previously connected, signal disconnected
            {
                bConnected = false;                                               // Update state
                BroadcastDisconnectedOnGameThread(TEXT("Connection lost"));       // Fire OnDisconnected (unexpected)
            }

            // Exponential backoff before attempting reconnect
            ++ReconnectAttempt;                                                   // Increment attempt count
            const int32 BackoffSec = FMath::Min(1 << FMath::Min(ReconnectAttempt, 5), 30); // 1,2,4,8,16,32 capped to 30
            UE_LOG(LogTikTokPlugin, Warning, TEXT("Reconnecting in %d seconds (attempt %d)..."), BackoffSec, ReconnectAttempt); // Log backoff

            // Sleep in small slices so Stop() can interrupt promptly
            const float Slice = 0.1f;                                             // Sleep slice in seconds
            float Slept = 0.0f;                                                   // Accumulator
            while (!bStopRequested && Slept < BackoffSec)                         // Loop until backoff complete or stop
            {
                FPlatformProcess::Sleep(Slice);                                   // Sleep a short slice
                Slept += Slice;                                                   // Accumulate slept time
            }
        }

        // Manual disconnect path (Leaving while-loop because Stop() requested)
        if (bStopRequested)
        {
            BroadcastDisconnectedOnGameThread(TEXT("User requested disconnect"));  // Fire OnDisconnected (manual)
        }

        return 0;                                                                  // Thread finished
    }

    virtual void Stop() override { bStopRequested = true; }                       // Signal the thread to stop

private:
    std::atomic<bool> bStopRequested{ false };                                    // Stop flag set by Disconnect
    FString           Handle;                                                     // TikTok handle to connect as
    FString           ApiKey;                                                     // EulerStream API key to authenticate
};

// ---------- Static connection state ---------- //
static FTikTokWSRunnable* GTikTokWS = nullptr;                                    // Global runnable pointer
static FRunnableThread* GTikTokWSThread = nullptr;                                // Global thread pointer

// ---------- UCLASS boilerplate ctor ---------- //
UTikTokPluginBPLibrary::UTikTokPluginBPLibrary(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)                                                     // Base UObject init
{
}

// ---------- Public BP API ---------- //

void UTikTokPluginBPLibrary::ConnectToTikTokLive(
    const FString& Handle,          // TikTok handle
    const FString& ApiKey,          // API key provided by the user
    bool bAutoCacheProfilePictures, // Whether to auto-download avatars
    ETikTokTeamMode TeamMode,       // Initial team mode selection
    EGiftAutoQueueMode AutoQueueMode)// Gift auto-queue selection
{
    if (UTikTokUserRegistry* Reg = UTikTokUserRegistry::Get())                   // Access the registry singleton
    {
        Reg->SetAutoCacheAvatars(bAutoCacheProfilePictures);                     // Configure avatar auto-cache
        Reg->SetTeamMode(TeamMode);                                              // Set team mode up front
    }

    // Initialize/Configure the Gift Queue Manager with the requested auto-queue mode
    if (UTikTokGiftQueueManager* GM = UTikTokGiftQueueManager::Get())            // Get manager singleton
    {
        GM->Initialize(AutoQueueMode);                                           // Apply mode & (optionally) pause/resume policy
    }

    if (GTikTokWSThread)                                                         // Prevent duplicate connections
    {
        UE_LOG(LogTikTokPlugin, Warning, TEXT("ConnectToTikTokLive: Already connected or connecting.")); // Warn duplicate
        return;                                                                   // Early out
    }

    GetTikTokEventHandler();                                                      // Ensure the event handler exists (rooted)

    // Validate API key early to give helpful logs before thread starts
    if (ApiKey.TrimStartAndEnd().IsEmpty())                                       // Empty API key guard
    {
        UE_LOG(LogTikTokPlugin, Error, TEXT("ConnectToTikTokLive: API key is empty. Visit Eulerstream.com for free API key --Aborting connection.")); // Log error
        return;                                                                   // Do not create the thread
    }

    GTikTokWS = new FTikTokWSRunnable(Handle, ApiKey);                            // Allocate runnable with handle+key
    GTikTokWSThread = FRunnableThread::Create(GTikTokWS,                          // Create the background thread
        TEXT("EulerStreamWebSocketThread"), // Thread name
        0,                                  // Stack size (default)
        TPri_BelowNormal);                  // Priority

    if (!GTikTokWSThread)                                                         // If thread creation failed
    {
        UE_LOG(LogTikTokPlugin, Error, TEXT("Failed to create websocket thread.")); // Log failure
        delete GTikTokWS; GTikTokWS = nullptr;                                    // Clean up runnable
    }
}

void UTikTokPluginBPLibrary::DisconnectFromTikTokLive()
{
    if (!GTikTokWSThread || !GTikTokWS)                                           // Nothing to disconnect
    {
        UE_LOG(LogTikTokPlugin, Warning, TEXT("DisconnectFromTikTokLive: No active connection.")); // Warn no-op
        return;                                                                   // Early out
    }

    GTikTokWS->Stop();                                                            // Signal the runnable to stop
    GTikTokWSThread->WaitForCompletion();                                         // Wait for the thread to exit
    delete GTikTokWSThread;  GTikTokWSThread = nullptr;                           // Free the thread object
    delete GTikTokWS;        GTikTokWS = nullptr;                                 // Free the runnable

    UE_LOG(LogTikTokPlugin, Log, TEXT("Disconnected from EulerStream."));         // Log clean disconnect
}

float UTikTokPluginBPLibrary::TikTokPluginSampleFunction(float /*Param*/)
{
    return -1.0f;                                                                  // Placeholder impl
}

UTikTokPluginEventHandler* UTikTokPluginBPLibrary::GetTikTokEventHandler()
{
    if (!StaticEventHandler)                                                       // If not created yet
    {
        StaticEventHandler = NewObject<UTikTokPluginEventHandler>();               // Create a new handler UObject
        if (StaticEventHandler)
        {
            StaticEventHandler->AddToRoot();                                       // Root it so GC will not collect it
        }
        else
        {
            UE_LOG(LogTikTokPlugin, Error, TEXT("Failed to create TikTok Event Handler.")); // Log allocation failure
        }
    }
    return StaticEventHandler;                                                     // Return the singleton
}

UTikTokUserRegistry* UTikTokPluginBPLibrary::GetUserRegistry()
{
    return UTikTokUserRegistry::Get();                                             // Return the registry singleton
}

UTikTokGiftQueueManager* UTikTokPluginBPLibrary::GetGiftQueueManager()
{
    return UTikTokGiftQueueManager::Get();                                         // Return the manager singleton
}

// --- Teams wrappers ---
void UTikTokPluginBPLibrary::SetTeamMode(ETikTokTeamMode InMode)
{
    if (UTikTokUserRegistry* Reg = UTikTokUserRegistry::Get())                     // Access registry
    {
        Reg->SetTeamMode(InMode);                                                  // Apply team mode
    }
}

ETikTokTeamMode UTikTokPluginBPLibrary::GetTeamMode()
{
    if (UTikTokUserRegistry* Reg = UTikTokUserRegistry::Get())                     // Access registry
    {
        return Reg->GetTeamMode();                                                 // Return team mode
    }
    return ETikTokTeamMode::None;                                                  // Default if missing
}
