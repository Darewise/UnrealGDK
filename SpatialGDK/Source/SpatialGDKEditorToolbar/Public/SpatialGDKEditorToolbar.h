// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Async/Future.h"
#include "CoreMinimal.h"
#include "LocalDeploymentManager.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonWriter.h"
#include "Templates/SharedPointer.h"
#include "TickableEditorObject.h"
#include "UObject/UnrealType.h"
#include "Widgets/Notifications/SNotificationList.h"

class FMenuBuilder;
class FSpatialGDKEditor;
class FToolBarBuilder;
class FUICommandList;
class SSpatialGDKSimulatedPlayerDeployment;
class SWindow;
class USoundBase;
class FMonitoredProcess;

struct FWorkerTypeLaunchSection;

DECLARE_LOG_CATEGORY_EXTERN(LogSpatialGDKEditorToolbar, Log, All);

class FSpatialGDKEditorToolbarModule : public IModuleInterface, public FTickableEditorObject
{
public:
	FSpatialGDKEditorToolbarModule();

	void StartupModule() override;
	void ShutdownModule() override;
	void PreUnloadCallback() override;

	/** FTickableEditorObject interface */
	void Tick(float DeltaTime) override;
	bool IsTickable() const override
	{
		return true;
	}

	TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FSpatialGDKEditorToolbarModule, STATGROUP_Tickables);
	}

	void OnShowSuccessNotification(const FString& NotificationText);
	void OnShowFailedNotification(const FString& NotificationText);
	void OnShowTaskStartNotification(const FString& NotificationText);

private:
	void MapActions(TSharedPtr<FUICommandList> PluginCommands);
	void SetupToolbar(TSharedPtr<FUICommandList> PluginCommands);
	void AddToolbarExtension(FToolBarBuilder& Builder);
	void AddMenuExtension(FMenuBuilder& Builder);
	TSharedRef<SWidget> GenerateComboMenu();

	void VerifyAndStartDeployment();

	void StartSpatialDeploymentButtonClicked();
	void StopSpatialDeploymentButtonClicked();

	void StartSpatialServiceButtonClicked();
	void StopSpatialServiceButtonClicked();

	bool StartSpatialDeploymentIsVisible() const;
	bool StartSpatialDeploymentCanExecute() const;

	bool StopSpatialDeploymentIsVisible() const;
	bool StopSpatialDeploymentCanExecute() const;

	bool LaunchInspectorCanExecute() const;

	bool StartSpatialServiceIsVisible() const;
	bool StartSpatialServiceCanExecute() const;

	bool StopSpatialServiceIsVisible() const;
	bool StopSpatialServiceCanExecute() const;

	void LaunchInspectorWebpageButtonClicked();
	void CreateSnapshotButtonClicked();
	void SchemaGenerateButtonClicked();
	void SchemaGenerateFullButtonClicked();
	void DeleteSchemaDatabaseButtonClicked();
	void OnPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);

	void LoadSublevels();
	bool CanLoadSublevels() const;
	FText LoadSublevelsTooltip() const;
	void CookMap();
	bool CanCookMap() const;
	FText CookMapLabel() const;
	FText CookMapTooltip() const;
	void LaunchDedicatedServer();
	bool CanLaunchDedicatedServer() const;
	FText LaunchDedicatedServerTooltip() const;
	void LaunchNetworkedClient();
	bool CanLaunchNetworkedClient() const;
	FText LaunchNetworkedClientTooltip() const;
	void ExploreDedicatedServerLogs() const;
	void ExploreNetworkedClientLogs();
	void ShowSpatialSettings();
	void ShowMapsSettings();
	void PackageNetworkedClient();
	bool CanPackageNetworkedClient() const;
	FText PackageNetworkedClientTooltip() const;

	void ShowSimulatedPlayerDeploymentDialog();
	bool ShowDeploymentDialogIsVisible() const;
	void OpenLaunchConfigurationEditor();

private:
	bool CanExecuteSchemaGenerator() const;
	bool GenericSpatialOSIsVisible() const;
	bool CanExecuteSnapshotGenerator() const;
	bool CreateSnapshotIsVisible() const;

	TSharedRef<SWidget> CreateGenerateSchemaMenuContent();
	TSharedRef<SWidget> CreateLaunchDeploymentMenuContent();

	void ShowTaskStartNotification(const FString& NotificationText);

	void ShowSuccessNotification(const FString& NotificationText);

	void ShowFailedNotification(const FString& NotificationText);

	bool FillWorkerLaunchConfigFromWorldSettings(UWorld& World, FWorkerTypeLaunchSection& OutLaunchConfig, FIntPoint& OutWorldDimension);

	void GenerateSchema(bool bFullScan);

	bool IsSnapshotGenerated() const;
	bool IsSchemaGenerated() const;

	FString GetOptionalExposedRuntimeIP() const;

	static void ShowCompileLog();

	TSharedPtr<FUICommandList> PluginCommands;
	FDelegateHandle OnPropertyChangedDelegateHandle;
	bool bStopSpatialOnExit;

	bool bSchemaBuildError;

	TWeakPtr<SNotificationItem> TaskNotificationPtr;

	// CORVUS_BEGIN Local Workflow
	FProcHandle ServerProcessHandle;
	FProcHandle AIServerProcessHandle;
	TSharedPtr<FMonitoredProcess> CookMapProcess;
	TSharedPtr<FMonitoredProcess> PackageClientProcess;
	// CORVUS_END

	// Sounds used for execution of tasks.
	USoundBase* ExecutionStartSound;
	USoundBase* ExecutionSuccessSound;
	USoundBase* ExecutionFailSound;

	TFuture<bool> SchemaGeneratorResult;
	TSharedPtr<FSpatialGDKEditor> SpatialGDKEditorInstance;

	TSharedPtr<SWindow> SimulatedPlayerDeploymentWindowPtr;
	TSharedPtr<SSpatialGDKSimulatedPlayerDeployment> SimulatedPlayerDeploymentConfigPtr;
	
	FLocalDeploymentManager* LocalDeploymentManager;
};
