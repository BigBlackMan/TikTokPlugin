// TikTokJsonParser.h
#pragma once // Include guard to prevent multiple inclusion

#include "CoreMinimal.h" // UE basics (FString, TArray, logs, etc.)
class UTikTokPluginEventHandler; // Forward declaration of the handler class (UObject)

/**
 * Stateless parser that inspects each payload and dispatches Blueprint events on the GameThread.
 * We keep this class header minimal; all work happens in the .cpp.
 */
class FTikTokJsonParser
{
public:
    // Parse the JSON payload and emit the appropriate Blueprint events via Handler. // Main entry point
    static void ParseAndDispatchAll(const FString& JsonText, UTikTokPluginEventHandler* Handler); // Handler may be null (we early-out)
};
