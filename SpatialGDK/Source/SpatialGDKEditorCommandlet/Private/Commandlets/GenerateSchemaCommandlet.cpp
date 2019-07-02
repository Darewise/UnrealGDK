// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "GenerateSchemaCommandlet.h"
#include "SpatialConstants.h"
#include "SpatialGDKEditor.h"
#include "SpatialGDKEditorCommandletPrivate.h"
#include "SpatialGDKEditorSchemaGenerator.h"

#include "Engine/ObjectLibrary.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/Paths.h"

using namespace SpatialGDKEditor::Schema;

UGenerateSchemaCommandlet::UGenerateSchemaCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

bool UGenerateSchemaCommandlet::HandleOptions(const TArray<FString>& Switches)
{
	if (Switches.Contains("delete-schema-db"))
	{
		if (DeleteSchemaDatabase(SpatialConstants::SCHEMA_DATABASE_FILE_PATH))
		{
			UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Deleted schema database"));
		}
		else
		{
			UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Failed to delete schema database"));
			return false;
		}
	}
	return true;
}

int32 UGenerateSchemaCommandlet::Main(const FString& Args)
{
	UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Schema Generation Commandlet Started"));

	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, GIsRunningUnattendedScript || IsRunningCommandlet());

	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> Params;
	ParseCommandLine(*Args, Tokens, Switches, Params);

	if (!HandleOptions(Switches))
	{
		UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Schema generation aborted"));
		return 1;
	}

	// CORVUS_BEGIN
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
