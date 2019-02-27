// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Async/Future.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonWriter.h"
#include "Templates/SharedPointer.h"
#include "TickableEditorObject.h"
#include "UObject/UnrealType.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "CloudDeploymentConfiguration.h"
#include "LocalDeploymentManager.h"

class FMenuBuilder;
class FSpatialGDKEditor;
class FToolBarBuilder;
class FUICommandList;
class SSpatialGDKCloudDeploymentConfiguration;
class SWindow;
class USoundBase;
class FMonitoredProcess;

struct FWorkerTypeLaunchSection;
class UAbstractRuntimeLoadBalancingStrategy;

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

	void OnShowSingleFailureNotification(const FString& NotificationText);
	void OnShowSuccessNotification(const FString& NotificationText);
	void OnShowFailedNotification(const FString& NotificationText);
	void OnShowTaskStartNotification(const FString& NotificationText);

	FReply OnStartCloudDeployment();
	bool CanStartCloudDeployment() const;

	bool IsSimulatedPlayersEnabled() const;
	/** Delegate called when the user either clicks the simulated players checkbox */
	void OnCheckedSimulatedPlayers();

	bool IsBuildClientWorkerEnabled() const;
	void OnCheckedBuildClientWorker();

private:
	void MapActions(TSharedPtr<FUICommandList> PluginCommands);
	void SetupToolbar(TSharedPtr<FUICommandList> PluginCommands);
	void AddToolbarExtension(FToolBarBuilder& Builder);
	void AddMenuExtension(FMenuBuilder& Builder);
	TSharedRef<SWidget> GenerateComboMenu();

	void VerifyAndStartDeployment();

	void StartLocalSpatialDeploymentButtonClicked();
	void StopSpatialDeploymentButtonClicked();

	void StartSpatialServiceButtonClicked();
	void StopSpatialServiceButtonClicked();

	bool StartNativeIsVisible() const;
	bool StartNativeCanExecute() const;

	bool StartLocalSpatialDeploymentIsVisible() const;
	bool StartLocalSpatialDeploymentCanExecute() const;

	bool StartCloudSpatialDeploymentIsVisible() const;
	bool StartCloudSpatialDeploymentCanExecute() const;

	bool StopSpatialDeploymentIsVisible() const;
	bool StopSpatialDeploymentCanExecute() const;

	bool LaunchInspectorCanExecute() const;

	bool StartSpatialServiceIsVisible() const;
	bool StartSpatialServiceCanExecute() const;

	bool StopSpatialServiceIsVisible() const;
	bool StopSpatialServiceCanExecute() const;

	void OnToggleSpatialNetworking();
	bool OnIsSpatialNetworkingEnabled() const;

	void GDKEditorSettingsClicked() const;
	void GDKRuntimeSettingsClicked() const;

	bool IsLocalDeploymentSelected() const;
	bool IsCloudDeploymentSelected() const;

	bool IsSpatialOSNetFlowConfigurable() const;

	void LocalDeploymentClicked();
	void CloudDeploymentClicked();

	bool IsLocalDeploymentIPEditable() const;
	bool AreCloudDeploymentPropertiesEditable() const;

	void LaunchInspectorWebpageButtonClicked();
	void CreateSnapshotButtonClicked();
	void SchemaGenerateButtonClicked();
	void SchemaGenerateFullButtonClicked();
	void DeleteSchemaDatabaseButtonClicked();
	void OnPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);

	// CORVUS_BEGIN
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
	// CORVUS_END

	void ShowCloudDeploymentDialog();
	bool ShowDeploymentDialogIsVisible() const;
	void OpenLaunchConfigurationEditor(); // CORVUS
	void LaunchOrShowCloudDeployment();

	/** Delegate to determine the 'Start Deployment' button enabled state */
	bool IsDeploymentConfigurationValid() const;
	bool CanBuildAndUpload() const;

	void OnBuildSuccess();
	void OnStartCloudDeploymentFinished();

	void AddDeploymentTagIfMissing(const FString& TagToAdd);

private:
	bool CanExecuteSchemaGenerator() const;
	bool GenericSpatialOSIsVisible() const;
	bool CanExecuteSnapshotGenerator() const;
	bool CreateSnapshotIsVisible() const;

	TSharedRef<SWidget> CreateGenerateSchemaMenuContent();
	TSharedRef<SWidget> CreateLaunchDeploymentMenuContent();
	TSharedRef<SWidget> CreateStartDropDownMenuContent();

	void ShowSingleFailureNotification(const FString& NotificationText);
	void ShowTaskStartNotification(const FString& NotificationText);

	void ShowSuccessNotification(const FString& NotificationText);

	void ShowFailedNotification(const FString& NotificationText);

	void GenerateSchema(bool bFullScan);

	bool IsSnapshotGenerated() const;

	FString GetOptionalExposedRuntimeIP() const;

	// This should be called whenever the settings determining whether a local deployment should be automatically started have changed.
	void OnAutoStartLocalDeploymentChanged();

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

	TSharedPtr<SWindow> CloudDeploymentSettingsWindowPtr;
	TSharedPtr<SSpatialGDKCloudDeploymentConfiguration> CloudDeploymentConfigPtr;
	
	FLocalDeploymentManager* LocalDeploymentManager;

	TFuture<bool> AttemptSpatialAuthResult;

	FCloudDeploymentConfiguration CloudDeploymentConfiguration;

	bool bStartingCloudDeployment;

	void GenerateConfigFromCurrentMap();
};
