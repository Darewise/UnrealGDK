// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "GenerateSchemaAndSnapshotsCommandlet.h"
#include "SpatialGDKEditorCommandletPrivate.h"
#include "SpatialGDKEditor.h"

#include "Kismet/GameplayStatics.h"
#include "Engine/ObjectLibrary.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/Paths.h"

UGenerateSchemaAndSnapshotsCommandlet::UGenerateSchemaAndSnapshotsCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UGenerateSchemaAndSnapshotsCommandlet::Main(const FString& Args)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> Params;
	ParseCommandLine(*Args, Tokens, Switches, Params);

	UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Schema & Snapshot Generation Commandlet Started - Args: %s"), *Args);

	FSpatialGDKEditor SpatialGDKEditor;
	GenerateSchema(SpatialGDKEditor);

	const FString* OptionnalMapName = Params.Find(TEXT("Map"));
	GenerateSnapshots(SpatialGDKEditor, OptionnalMapName);

	UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Schema & Snapshot Generation Commandlet Complete"));

	return 0;
}

void UGenerateSchemaAndSnapshotsCommandlet::GenerateSchema(FSpatialGDKEditor& SpatialGDKEditor)
{
	SpatialGDKEditor.GenerateSchema(
		FSimpleDelegate::CreateLambda([]() { UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Schema Generation Completed!")); }),
		FSimpleDelegate::CreateLambda([]() { UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Schema Generation Failed")); }),
		FSpatialGDKEditorErrorHandler::CreateLambda([](FString ErrorText) { UE_LOG(LogSpatialGDKEditorCommandlet, Error, TEXT("%s"), *ErrorText); }));
	while (SpatialGDKEditor.IsSchemaGeneratorRunning())
		FPlatformProcess::Sleep(0.1f);
}

void UGenerateSchemaAndSnapshotsCommandlet::GenerateSnapshots(FSpatialGDKEditor& SpatialGDKEditor, const FString* InMapName /* = nullptr */)
{
	const FString MapDir = TEXT("/Game");
	const TArray<FString> MapFilePaths = GetAllMapPaths(MapDir);
	for (FString MapFilePath : MapFilePaths)
	{
		if (!InMapName || (InMapName && MapFilePath.EndsWith(*InMapName)))
		{
			GenerateSnapshotForMap(SpatialGDKEditor, MapFilePath);
			if (InMapName)
				break;
		}
	}
}

TArray<FString> UGenerateSchemaAndSnapshotsCommandlet::GetAllMapPaths(const FString& InMapsPath)
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
		UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("\t%s"), *Path);
		Paths.Add(MoveTemp(Path));
	}

	return Paths;
}

void UGenerateSchemaAndSnapshotsCommandlet::GenerateSnapshotForMap(FSpatialGDKEditor& SpatialGDKEditor, const FString& MapPath)
{
	UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Generating Snapshot for %s"), *MapPath);

	//Load the World
	if (!FEditorFileUtils::LoadMap(MapPath))
	{
		UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Failed to load map %s"), *MapPath);
	}

	//Generate the Snapshot!
	SpatialGDKEditor.GenerateSnapshot(
		GWorld, FPaths::SetExtension(FPaths::GetCleanFilename(MapPath), TEXT(".snapshot")),
		FSimpleDelegate::CreateLambda([]() { UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Success!")); }),
		FSimpleDelegate::CreateLambda([]() { UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Failed")); }),
		FSpatialGDKEditorErrorHandler::CreateLambda([](FString ErrorText) { UE_LOG(LogSpatialGDKEditorCommandlet, Error, TEXT("%s"), *ErrorText); }));
}
