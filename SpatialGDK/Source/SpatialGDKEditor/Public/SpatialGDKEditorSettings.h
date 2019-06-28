// Copyright (c) Improbable Worlds Ltd, All Rights Reserved
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Misc/Paths.h"

#include "SpatialGDKEditorSettings.generated.h"

UCLASS(config = SpatialGDKEditorSettings, defaultconfig)
class SPATIALGDKEDITOR_API USpatialGDKEditorSettings : public UObject
{
	GENERATED_BODY()

public:
	USpatialGDKEditorSettings(const FObjectInitializer& ObjectInitializer);

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostInitProperties() override;

private:
	/** Path to the directory containing the SpatialOS-related files. */
	UPROPERTY(EditAnywhere, config, Category = "General", meta = (ConfigRestartRequired = false, DisplayName = "SpatialOS directory"))
	FDirectoryPath SpatialOSDirectory;

public:
	/** If checked, all dynamically spawned entities will be deleted when server workers disconnect. */
	UPROPERTY(EditAnywhere, config, Category = "Play in editor settings", meta = (ConfigRestartRequired = false, DisplayName = "Delete dynamically spawned entities"))
	bool bDeleteDynamicEntities;

	/** If checked, a launch configuration will be generated by default when launching spatial through the toolbar. */
	UPROPERTY(EditAnywhere, config, Category = "Launch", meta = (ConfigRestartRequired = false, DisplayName = "Generate default launch config"))
	bool bGenerateDefaultLaunchConfig;

private:
	/** Launch configuration file used for `spatial local launch`. */
	UPROPERTY(EditAnywhere, config, Category = "Launch", meta = (EditCondition = "!bGenerateDefaultLaunchConfig", ConfigRestartRequired = false, DisplayName = "Launch configuration"))
	FFilePath SpatialOSLaunchConfig;

public:
	/** Stop `spatial local launch` when shutting down editor. */
	UPROPERTY(EditAnywhere, config, Category = "Launch", meta = (ConfigRestartRequired = false, DisplayName = "Stop on exit"))
	bool bStopSpatialOnExit;

private:
	/** Path to your SpatialOS snapshot. */
	UPROPERTY(EditAnywhere, config, Category = "Snapshots", meta = (ConfigRestartRequired = false, DisplayName = "Snapshot path"))
	FDirectoryPath SpatialOSSnapshotPath;

	/** Name snapshot file like current level. */
	UPROPERTY(EditAnywhere, config, Category = "Snapshots", meta = (ConfigRestartRequired = false, DisplayName = "Name Snapshots like levels"))
	bool bSnapshotUseCurrentLevelName = true;

	/** Name of your SpatialOS snapshot file. */
	UPROPERTY(EditAnywhere, config, Category = "Snapshots", meta = (ConfigRestartRequired = false, DisplayName = "Snapshot file name"))
	FString SpatialOSSnapshotFile;

	/** If checked, the GDK creates a launch configuration file by default when you launch a local deployment through the toolbar. */
	UPROPERTY(EditAnywhere, config, Category = "Schema", meta = (ConfigRestartRequired = false, DisplayName = "Output path for the generated schemas"))
	FDirectoryPath GeneratedSchemaOutputFolder;

	/** Command line flags passed in to `spatial local launch`.*/
	UPROPERTY(EditAnywhere, config, Category = "Launch", meta = (ConfigRestartRequired = false, DisplayName = "Command line flags for local launch"))
	TArray<FString> SpatialOSCommandLineLaunchFlags;

public:

	// CORVUS_BEGIN

	/** Additional command line flags passed in to the Dedicated Server in Local Workflow. */
	UPROPERTY(EditAnywhere, config, Category = "Local Workflow", meta = (ConfigRestartRequired = false, DisplayName = "Command line flags for Dedicated Server"))
	FString LocalWorflowServerCommandLineFlags;

	/** Additional command line flags passed in to the Networked Client in Local Workflow. By default "-windowed -ResX=960 -ResY=540". */
	UPROPERTY(EditAnywhere, config, Category = "Local Workflow", meta = (ConfigRestartRequired = false, DisplayName = "Command line flags for Networked Client"))
	FString LocalWorflowClientCommandLineFlags;

	/** IP Address of the Dedicated Server in Local Workflow. Only needed to connect from a remote computer. By default 127.0.0.1 */
	UPROPERTY(EditAnywhere, config, Category = "Local Workflow", meta = (ConfigRestartRequired = false, DisplayName = "IP Address Dedicated Server"))
	FString LocalWorflowServerIpAddr;

	// CORVUS_END

	/** If checked, placeholder entities will be added to the snapshot on generation */
	UPROPERTY(EditAnywhere, config, Category = "Snapshots", meta = (ConfigRestartRequired = false, DisplayName = "Generate placeholder entities in snapshot"))
	bool bGeneratePlaceholderEntitiesInSnapshot;

	FORCEINLINE FString GetSpatialOSDirectory() const
	{
		return SpatialOSDirectory.Path.IsEmpty()
			? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("/../spatial/")))
			: SpatialOSDirectory.Path;
	}

	FORCEINLINE FString GetSpatialOSLaunchConfig() const
	{
		return SpatialOSLaunchConfig.FilePath.IsEmpty()
			? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("/../spatial/default_launch.json")))
			: SpatialOSLaunchConfig.FilePath;
	}

	FORCEINLINE bool IsSnapshotUsingCurrentLevelName() const
	{
		return bSnapshotUseCurrentLevelName;
	}

	FORCEINLINE FString GetSpatialOSSnapshotFile() const
	{
		return SpatialOSSnapshotFile.IsEmpty()
			? FString(TEXT("default.snapshot"))
			: SpatialOSSnapshotFile;
	}

	FORCEINLINE FString GetSpatialOSSnapshotFolderPath() const
	{
		return SpatialOSSnapshotPath.Path.IsEmpty()
			? FPaths::ConvertRelativePathToFull(FPaths::Combine(GetSpatialOSDirectory(), TEXT("../spatial/snapshots/")))
			: SpatialOSSnapshotPath.Path;
	}

	FORCEINLINE FString GetGeneratedSchemaOutputFolder() const
	{
		return GeneratedSchemaOutputFolder.Path.IsEmpty()
			? FPaths::ConvertRelativePathToFull(FPaths::Combine(GetSpatialOSDirectory(), FString(TEXT("schema/unreal/generated/"))))
			: GeneratedSchemaOutputFolder.Path;
	}

	FORCEINLINE FString GetSpatialOSCommandLineLaunchFlags() const
	{
		FString CommandLineLaunchFlags = TEXT("");

		for (FString Flag : SpatialOSCommandLineLaunchFlags)
		{
			Flag = Flag.StartsWith(TEXT("--")) ? Flag : TEXT("--") + Flag;
			CommandLineLaunchFlags += Flag + TEXT(" ");
		}

		return CommandLineLaunchFlags;
	}

	virtual FString ToString();
};
