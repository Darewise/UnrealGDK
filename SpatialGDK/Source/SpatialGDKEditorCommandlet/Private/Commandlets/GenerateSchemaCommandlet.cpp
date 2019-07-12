// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "GenerateSchemaCommandlet.h"
#include "SpatialGDKEditorCommandletPrivate.h"
#include "SpatialGDKEditor.h"

#include "Engine/LevelStreaming.h"
#include "Engine/ObjectLibrary.h"
#include "Engine/World.h"
#include "Engine/WorldComposition.h"
#include "FileHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"

UGenerateSchemaCommandlet::UGenerateSchemaCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UGenerateSchemaCommandlet::Main(const FString& Args)
{
	UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Schema Generation Commandlet Started"));

	// CORVUS_BEGIN
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> Params;
	ParseCommandLine(*Args, Tokens, Switches, Params);

	const FString* MapName = Params.Find(TEXT("Map"));
	if (MapName)
	{
		const FString MapDir = TEXT("/Game");
		const TArray<FString> MapFilePaths = GetAllMapPaths(MapDir);
		for (FString MapFilePath : MapFilePaths)
		{
			if (MapFilePath.EndsWith(*MapName))
			{
				// Load the World
				UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("LoadMap %s"), *MapFilePath);
				if (!FEditorFileUtils::LoadMap(MapFilePath))
				{
					UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Failed to load map %s"), *MapFilePath);
				}

				// Ensure all world composition streaming levels are also loaded
				if (GWorld->WorldComposition)
				{
					// Get the list of all distance-dependent streaming (excluding levels with streaming disabled)
					// sub-levels visible and hidden levels from current view point
					TArray<FDistanceVisibleLevel> distanceVisibleLevels;
					{
						TArray<FDistanceVisibleLevel> distanceHiddenLevels;
						const FVector originLocation(0.f);
						GWorld->WorldComposition->GetDistanceVisibleLevels(originLocation, distanceVisibleLevels, distanceHiddenLevels);
						// merge both lists in one, we need to load everything to take a full snapshot
						distanceVisibleLevels += MoveTemp(distanceHiddenLevels);
					}

					UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Loading Streaming levels..."));
					for (const auto& distanceVisibleLevel : distanceVisibleLevels)
					{
						if (distanceVisibleLevel.StreamingLevel && !distanceVisibleLevel.StreamingLevel->IsLevelLoaded())
						{
							const TArray<ULevelStreaming*> streamingLevels({ distanceVisibleLevel.StreamingLevel });
							GWorld->AddStreamingLevels(streamingLevels);
							GWorld->FlushLevelStreaming(EFlushLevelStreamingType::Full);
						}
					}
					UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("All streaming Levels Loaded"));
				}

				break;
			}
		}
	}
	const bool bFullScan = (MapName == nullptr);
	// CORVUS_END

	//Generate Schema!
	bool bSchemaGenSuccess;
	FSpatialGDKEditor SpatialGDKEditor;
	if (SpatialGDKEditor.GenerateSchema(bFullScan)) // CORVUS
	{
		UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Schema Generation Completed!"));
		bSchemaGenSuccess = true;
	}
	else
	{
		UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Schema Generation Failed"));
		bSchemaGenSuccess = false;
	}

	UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Schema Generation Commandlet Complete"));

	return bSchemaGenSuccess ? 0 : 1;
}

// CORVUS_BEGIN
TArray<FString> UGenerateSchemaCommandlet::GetAllMapPaths(FString InMapsPath)
{
	UObjectLibrary* ObjectLibrary = UObjectLibrary::CreateLibrary(UWorld::StaticClass(), false, true);
	ObjectLibrary->LoadAssetDataFromPath(InMapsPath);
	TArray<FAssetData> AssetDatas;
	ObjectLibrary->GetAssetDataList(AssetDatas);
	UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Found %d maps:"), AssetDatas.Num());

	TArray<FString> Paths = TArray<FString>();
	for (FAssetData& AssetData : AssetDatas)
	{
		FString Path = AssetData.PackageName.ToString();
		Paths.Add(Path);
		UE_LOG(LogSpatialGDKEditorCommandlet, Log, TEXT("\t%s"), *Path);
	}

	return Paths;
}
// CORVUS_END
