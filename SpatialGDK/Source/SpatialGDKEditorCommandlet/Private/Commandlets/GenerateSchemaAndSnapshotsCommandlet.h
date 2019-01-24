// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Commandlets/Commandlet.h"

#include "GenerateSchemaAndSnapshotsCommandlet.generated.h"

class FSpatialGDKEditor;

UCLASS()
class UGenerateSchemaAndSnapshotsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGenerateSchemaAndSnapshotsCommandlet();

public:
	virtual int32 Main(const FString& Params) override;

private:
	void GenerateSchema(FSpatialGDKEditor& SpatialGDKEditor);
	void GenerateSnapshots(FSpatialGDKEditor& SpatialGDKEditor, const FString* InMapName = nullptr);
	TArray<FString> GetAllMapPaths(const FString& InMapsPath);
	void GenerateSnapshotForMap(FSpatialGDKEditor& SpatialGDKEditor, const FString& WorldPath);
};
