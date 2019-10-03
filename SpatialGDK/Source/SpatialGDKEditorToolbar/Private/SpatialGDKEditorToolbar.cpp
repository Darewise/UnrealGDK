// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialGDKEditorToolbar.h"

#include "Async/Async.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ISettingsContainer.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "LevelEditor.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Serialization/JsonWriter.h"
#include "SpatialGDKEditorToolbarCommands.h"
#include "SpatialGDKEditorToolbarStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "SpatialConstants.h"
#include "SpatialGDKEditor.h"
#include "SpatialGDKEditorSettings.h"
#include "SpatialGDKServicesModule.h"
#include "SpatialGDKSettings.h"
#include "SpatialGDKSimulatedPlayerDeployment.h"

#include "Editor/EditorEngine.h"
#include "HAL/FileManager.h"
#include "Sound/SoundBase.h"

#include "AssetRegistryModule.h"
#include "GeneralProjectSettings.h"
#include "Kismet/GameplayStatics.h"
#include "LevelEditor.h"
#include "Misc/FileHelper.h"

// CORVUS_BEGIN
#include "Misc/MonitoredProcess.h"
#include "Unreal/UnrealUtils.h"
#include "UnrealEditorUtils.h"
// CORVUS_END

DEFINE_LOG_CATEGORY(LogSpatialGDKEditorToolbar);

#define LOCTEXT_NAMESPACE "FSpatialGDKEditorToolbarModule"

FSpatialGDKEditorToolbarModule::FSpatialGDKEditorToolbarModule()
: bStopSpatialOnExit(false)
{
}

void FSpatialGDKEditorToolbarModule::StartupModule()
{
	FSpatialGDKEditorToolbarStyle::Initialize();
	FSpatialGDKEditorToolbarStyle::ReloadTextures();

	FSpatialGDKEditorToolbarCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);
	MapActions(PluginCommands);
	SetupToolbar(PluginCommands);

	// load sounds
	ExecutionStartSound = LoadObject<USoundBase>(nullptr, TEXT("/Engine/EditorSounds/Notifications/CompileStart_Cue.CompileStart_Cue"));
	ExecutionStartSound->AddToRoot();
	ExecutionSuccessSound = LoadObject<USoundBase>(nullptr, TEXT("/Engine/EditorSounds/Notifications/CompileSuccess_Cue.CompileSuccess_Cue"));
	ExecutionSuccessSound->AddToRoot();
	ExecutionFailSound = LoadObject<USoundBase>(nullptr, TEXT("/Engine/EditorSounds/Notifications/CompileFailed_Cue.CompileFailed_Cue"));
	ExecutionFailSound->AddToRoot();
	SpatialGDKEditorInstance = MakeShareable(new FSpatialGDKEditor());

	OnPropertyChangedDelegateHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FSpatialGDKEditorToolbarModule::OnPropertyChanged);
	bStopSpatialOnExit = GetDefault<USpatialGDKEditorSettings>()->bStopSpatialOnExit;

	FSpatialGDKServicesModule& GDKServices = FModuleManager::GetModuleChecked<FSpatialGDKServicesModule>("SpatialGDKServices");
	LocalDeploymentManager = GDKServices.GetLocalDeploymentManager();
	LocalDeploymentManager->SetAutoDeploy(GetDefault<USpatialGDKEditorSettings>()->bAutoStartLocalDeployment);

	// Bind the play button delegate to starting a local spatial deployment.
	if (!UEditorEngine::TryStartSpatialDeployment.IsBound() && GetDefault<USpatialGDKEditorSettings>()->bAutoStartLocalDeployment)
	{
		UEditorEngine::TryStartSpatialDeployment.BindLambda([this]
		{
			VerifyAndStartDeployment();
		});
	}
}

void FSpatialGDKEditorToolbarModule::ShutdownModule()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnPropertyChangedDelegateHandle);

	if (ExecutionStartSound != nullptr)
	{
		if (!GExitPurge)
		{
			ExecutionStartSound->RemoveFromRoot();
		}
		ExecutionStartSound = nullptr;
	}

	if (ExecutionSuccessSound != nullptr)
	{
		if (!GExitPurge)
		{
			ExecutionSuccessSound->RemoveFromRoot();
		}
		ExecutionSuccessSound = nullptr;
	}

	if (ExecutionFailSound != nullptr)
	{
		if (!GExitPurge)
		{
			ExecutionFailSound->RemoveFromRoot();
		}
		ExecutionFailSound = nullptr;
	}

	FSpatialGDKEditorToolbarStyle::Shutdown();
	FSpatialGDKEditorToolbarCommands::Unregister();
}

void FSpatialGDKEditorToolbarModule::PreUnloadCallback()
{
	if (bStopSpatialOnExit)
	{
		LocalDeploymentManager->TryStopLocalDeployment();
	}
}

void FSpatialGDKEditorToolbarModule::Tick(float DeltaTime)
{
	// CORVUS_BEGIN
	UnrealUtils::UpdateProcessHandle(ServerProcessHandle);
	if (CookMapProcess.IsValid() && !CookMapProcess->Update())
	{
		CookMapProcess.Reset();
	}
	if (PackageClientProcess.IsValid() && !PackageClientProcess->Update())
	{
		PackageClientProcess.Reset();
	}
	// CORVUS_END
}

bool FSpatialGDKEditorToolbarModule::CanExecuteSchemaGenerator() const
{
	return SpatialGDKEditorInstance.IsValid() && !SpatialGDKEditorInstance.Get()->IsSchemaGeneratorRunning() && !LocalDeploymentManager->IsLocalDeploymentRunning() && !CookMapProcess.IsValid();
}

bool FSpatialGDKEditorToolbarModule::CanExecuteSnapshotGenerator() const
{
	return SpatialGDKEditorInstance.IsValid() && !SpatialGDKEditorInstance.Get()->IsSchemaGeneratorRunning();
}

void FSpatialGDKEditorToolbarModule::MapActions(TSharedPtr<class FUICommandList> InPluginCommands)
{
	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSchema,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::SchemaGenerateButtonClicked),
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanExecuteSchemaGenerator));

	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSchemaFull,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::SchemaGenerateFullButtonClicked),
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanExecuteSchemaGenerator));

	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSnapshot,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CreateSnapshotButtonClicked),
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanExecuteSnapshotGenerator));

	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().StartSpatialDeployment,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartSpatialDeploymentButtonClicked),
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartSpatialDeploymentCanExecute),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartSpatialDeploymentIsVisible));

	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().StopSpatialDeployment,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StopSpatialDeploymentButtonClicked),
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StopSpatialDeploymentCanExecute),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StopSpatialDeploymentIsVisible));

	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().LaunchInspectorWebPageAction,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LaunchInspectorWebpageButtonClicked),
		FCanExecuteAction());

	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().OpenSimulatedPlayerConfigurationWindowAction,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::ShowSimulatedPlayerDeploymentDialog),
		FCanExecuteAction());
	
	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().StartSpatialService,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartSpatialServiceButtonClicked),
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartSpatialServiceCanExecute),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartSpatialServiceIsVisible));

	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().StopSpatialService,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StopSpatialServiceButtonClicked),
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StopSpatialServiceCanExecute),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StopSpatialServiceIsVisible));
}

void FSpatialGDKEditorToolbarModule::SetupToolbar(TSharedPtr<class FUICommandList> InPluginCommands)
{
	FLevelEditorModule& LevelEditorModule =
		FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	{
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());
		MenuExtender->AddMenuExtension(
			"General", EExtensionHook::After, InPluginCommands,
			FMenuExtensionDelegate::CreateRaw(this, &FSpatialGDKEditorToolbarModule::AddMenuExtension));

		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
	}

	{
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
		ToolbarExtender->AddToolBarExtension(
			"Game", EExtensionHook::Before, InPluginCommands,
			FToolBarExtensionDelegate::CreateRaw(this,
				&FSpatialGDKEditorToolbarModule::AddToolbarExtension));

		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	}
}

void FSpatialGDKEditorToolbarModule::AddMenuExtension(FMenuBuilder& Builder)
{
	Builder.BeginSection("SpatialOS Unreal GDK", LOCTEXT("SpatialOS Unreal GDK", "SpatialOS Unreal GDK"));
	{
		/* CORVUS: no interest of putting all these in menus, especially not in Window menu (should use a dedicated one)
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSchema);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSnapshot);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().StartSpatialDeployment);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().StopSpatialDeployment);
		*/
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().LaunchInspectorWebPageAction);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().OpenSimulatedPlayerConfigurationWindowAction);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().StartSpatialService);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().StopSpatialService);
	}
	Builder.EndSection();
}

void FSpatialGDKEditorToolbarModule::AddToolbarExtension(FToolBarBuilder& Builder)
{
	Builder.AddSeparator(NAME_None);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSchema);
	Builder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CreateGenerateSchemaMenuContent),
		LOCTEXT("GDKSchemaCombo_Label", "Schema Generation Options"),
		TAttribute<FText>(),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "GDK.Schema"),
		true
	);
	// Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSnapshot); // CORVUS: no more usage for snapshots since release-0.2.0!
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().StartSpatialDeployment);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().StopSpatialDeployment);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().LaunchInspectorWebPageAction);
	// Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().OpenSimulatedPlayerConfigurationWindowAction); // CORVUS: too dangerous to expose
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().StartSpatialService);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().StopSpatialService);

	// CORVUS_BEGIN
	Builder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateRaw(this, &FSpatialGDKEditorToolbarModule::GenerateComboMenu),
		LOCTEXT("LocalWorkflow_Short", "Local"),
		LOCTEXT("LocalWorkflow_ToolTip", "Local Workflow menu"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "PlayWorld.Simulate")
	);
	// CORVUS_END
}

TSharedRef<SWidget> FSpatialGDKEditorToolbarModule::CreateGenerateSchemaMenuContent()
{
	FMenuBuilder MenuBuilder(true, PluginCommands);
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("GDKSchemaOptionsHeader", "Schema Generation"));
	{
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSchemaFull);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

// CORVUS_BEGIN
TSharedRef<SWidget> FSpatialGDKEditorToolbarModule::GenerateComboMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, PluginCommands);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("LocalWorkflow", "Local Workflow"));
	{
		// tip section
		MenuBuilder.AddWidget(
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("LocalWorkflowTip", "Launch a dedicated server and game client (execute steps in order)."))
			.WrapTextAt(180),
			FText::GetEmpty());
		MenuBuilder.AddMenuEntry(
			LOCTEXT("LoadSublevels", "Load sub-levels"),
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LoadSublevelsTooltip)),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.WorldBrowser"),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LoadSublevels),
				FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanLoadSublevels)
			)
		);
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSchema);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CookMap", "Cook Current Map"),
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CookMapTooltip)),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Build"),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CookMap),
				FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanCookMap)
			)
		);
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().StartSpatialDeployment);
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().StopSpatialDeployment);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("LaunchDedicatedServer", "Launch Dedicated Server"),
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LaunchDedicatedServerTooltip)),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "PlayWorld.PlayInNewProcess"),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LaunchDedicatedServer),
				FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanLaunchDedicatedServer)
			)
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("LaunchNetworkedClient", "Launch Networked Client"),
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LaunchNetworkedClientTooltip)),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "PlayWorld.PlayInEditorFloating"),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LaunchNetworkedClient),
				FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanLaunchNetworkedClient)
			)
		);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ExploreDedicatedServerLogs", "Show Dedicated Server logs"),
			LOCTEXT("ExploreDedicatedServerLogsToolTip", "Open File Explorer to show log files of the Dedicated Server"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "MessageLog.TabIcon"),
			FUIAction(FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::ExploreDedicatedServerLogs))
		);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ExploreNetworkedClientLogs", "Show Networked Client logs"),
			LOCTEXT("ExploreDedicatedServerLogsToolTip", "Open File Explorer to show log files of the Networked Client"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "MessageLog.TabIcon"),
			FUIAction(FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::ExploreNetworkedClientLogs))
		);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("PackageNetworkedClient", "Package Networked Client"),
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FSpatialGDKEditorToolbarModule::PackageNetworkedClientTooltip)),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "MainFrame.PackageProject"),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::PackageNetworkedClient),
				FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanPackageNetworkedClient)
			)
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}
// CORVUS_END

void FSpatialGDKEditorToolbarModule::CreateSnapshotButtonClicked()
{
	OnShowTaskStartNotification("Started snapshot generation");

	const USpatialGDKEditorSettings* Settings = GetDefault<USpatialGDKEditorSettings>();

	SpatialGDKEditorInstance->GenerateSnapshot(
		GEditor->GetEditorWorldContext().World(), Settings->GetSpatialOSSnapshotFile(),
		FSimpleDelegate::CreateLambda([this]() { OnShowSuccessNotification("Snapshot successfully generated!"); }),
		FSimpleDelegate::CreateLambda([this]() { OnShowFailedNotification("Snapshot generation failed!"); }),
		FSpatialGDKEditorErrorHandler::CreateLambda([](FString ErrorText) { FMessageDialog::Debugf(FText::FromString(ErrorText)); }));
}

void FSpatialGDKEditorToolbarModule::SchemaGenerateButtonClicked()
{
	GenerateSchema(false);
}

void FSpatialGDKEditorToolbarModule::SchemaGenerateFullButtonClicked()
{
	GenerateSchema(true);
}		

void FSpatialGDKEditorToolbarModule::OnShowTaskStartNotification(const FString& NotificationText)
{
	AsyncTask(ENamedThreads::GameThread, [NotificationText]
	{
		if (FSpatialGDKEditorToolbarModule* Module = FModuleManager::GetModulePtr<FSpatialGDKEditorToolbarModule>("SpatialGDKEditorToolbar"))
		{
			Module->ShowTaskStartNotification(NotificationText);
		}
	});
}

void FSpatialGDKEditorToolbarModule::ShowTaskStartNotification(const FString& NotificationText)
{
	// If a task notification already exists then expire it.
	if (TaskNotificationPtr.IsValid())
	{
		TaskNotificationPtr.Pin()->ExpireAndFadeout();
	}

	if (GEditor && ExecutionStartSound)
	{
		GEditor->PlayEditorSound(ExecutionStartSound);
	}

	FNotificationInfo Info(FText::AsCultureInvariant(NotificationText));
	Info.Image = FSpatialGDKEditorToolbarStyle::Get().GetBrush(TEXT("SpatialGDKEditorToolbar.SpatialOSLogo"));
	Info.ExpireDuration = 5.0f;
	Info.bFireAndForget = false;

	TaskNotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);

	if (TaskNotificationPtr.IsValid())
	{
		TaskNotificationPtr.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FSpatialGDKEditorToolbarModule::OnShowSuccessNotification(const FString& NotificationText)
{
	AsyncTask(ENamedThreads::GameThread, [NotificationText]
	{
		if (FSpatialGDKEditorToolbarModule* Module = FModuleManager::GetModulePtr<FSpatialGDKEditorToolbarModule>("SpatialGDKEditorToolbar"))
		{
			Module->ShowSuccessNotification(NotificationText);
		}
	});
}

void FSpatialGDKEditorToolbarModule::ShowSuccessNotification(const FString& NotificationText)
{
	TSharedPtr<SNotificationItem> Notification = TaskNotificationPtr.Pin();
	if (Notification.IsValid())
	{
		Notification->SetFadeInDuration(0.1f);
		Notification->SetFadeOutDuration(0.5f);
		Notification->SetExpireDuration(5.0f);
		Notification->SetText(FText::AsCultureInvariant(NotificationText));
		Notification->SetCompletionState(SNotificationItem::CS_Success);
		Notification->ExpireAndFadeout();

		if (GEditor && ExecutionSuccessSound)
		{
			GEditor->PlayEditorSound(ExecutionSuccessSound);
		}
	}
}

void FSpatialGDKEditorToolbarModule::OnShowFailedNotification(const FString& NotificationText)
{
	AsyncTask(ENamedThreads::GameThread, [NotificationText]
	{
		if (FSpatialGDKEditorToolbarModule* Module = FModuleManager::GetModulePtr<FSpatialGDKEditorToolbarModule>("SpatialGDKEditorToolbar"))
		{
			Module->ShowFailedNotification(NotificationText);
		}
	});
}

void FSpatialGDKEditorToolbarModule::ShowFailedNotification(const FString& NotificationText)
{
	TSharedPtr<SNotificationItem> Notification = TaskNotificationPtr.Pin();
	if (Notification.IsValid())
	{
		Notification->SetFadeInDuration(0.1f);
		Notification->SetFadeOutDuration(0.5f);
		Notification->SetExpireDuration(5.0);
		Notification->SetText(FText::AsCultureInvariant(NotificationText));
		Notification->SetCompletionState(SNotificationItem::CS_Fail);
		Notification->ExpireAndFadeout();

		if (GEditor && ExecutionFailSound)
		{
			GEditor->PlayEditorSound(ExecutionFailSound);
		}
	}
}

bool FSpatialGDKEditorToolbarModule::ValidateGeneratedLaunchConfig() const
{
	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
	const USpatialGDKSettings* SpatialGDKRuntimeSettings = GetDefault<USpatialGDKSettings>();
	const FSpatialLaunchConfigDescription& LaunchConfigDescription = SpatialGDKEditorSettings->LaunchConfigDesc;

	if (const FString* EnableChunkInterest = LaunchConfigDescription.World.LegacyFlags.Find(TEXT("enable_chunk_interest")))
	{
		if (SpatialGDKRuntimeSettings->bUsingQBI && (*EnableChunkInterest == TEXT("true")))
		{
			const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("The legacy flag \"enable_chunk_interest\" is set to true in the generated launch configuration. This flag needs to be set to false when QBI is enabled.\n\nDo you want to configure your launch config settings now?")));

			if (Result == EAppReturnType::Yes)
			{
				FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Editor Settings");
			}

			return false;
		}
		else if (!SpatialGDKRuntimeSettings->bUsingQBI && (*EnableChunkInterest == TEXT("false")))
		{
			const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("The legacy flag \"enable_chunk_interest\" is set to false in the generated launch configuration. This flag needs to be set to true when QBI is disabled.\n\nDo you want to configure your launch config settings now?")));

			if (Result == EAppReturnType::Yes)
			{
				FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Editor Settings");
			}

			return false;
		}
	}
	else
	{
		const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("The legacy flag \"enable_chunk_interest\" is not specified in the generated launch configuration.\n\nDo you want to configure your launch config settings now?")));

		if (Result == EAppReturnType::Yes)
		{
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Editor Settings");
		}

		return false;
	}

	const ULevelEditorPlaySettings* LevelEditorPlaySettings = GetDefault<ULevelEditorPlaySettings>();
	if (!SpatialGDKRuntimeSettings->bEnableHandover && LevelEditorPlaySettings->GetTotalPIEServerWorkerCount() > 1)
	{
		const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("Property handover is disabled and multiple launch servers are specified.\nThis is not supported.\n\nDo you want to configure your project settings now?")));

		if (Result == EAppReturnType::Yes)
		{
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Runtime Settings");
		}

		return false;
	}

	if (!SpatialGDKRuntimeSettings->ServerWorkerTypes.Contains(SpatialGDKRuntimeSettings->DefaultWorkerType.WorkerTypeName))
	{
		const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("Default Worker Type is invalid, please choose a valid worker type as the default.\n\nDo you want to configure your project settings now?")));

		if (Result == EAppReturnType::Yes)
		{
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Runtime Settings");
		}

		return false;
	}

	if (SpatialGDKRuntimeSettings->bEnableOffloading)
	{
		for (const TPair<FName, FActorGroupInfo>& ActorGroup : SpatialGDKRuntimeSettings->ActorGroups)
		{
			if (!SpatialGDKRuntimeSettings->ServerWorkerTypes.Contains(ActorGroup.Value.OwningWorkerType.WorkerTypeName))
			{
				const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(FString::Printf(TEXT("Actor Group '%s' has an invalid Owning Worker Type, please choose a valid worker type.\n\nDo you want to configure your project settings now?"), *ActorGroup.Key.ToString())));

				if (Result == EAppReturnType::Yes)
				{
					FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Runtime Settings");
				}

				return false;
			}
		}
	}

	return true;
}

void FSpatialGDKEditorToolbarModule::StartSpatialServiceButtonClicked()
{
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this]
	{
		FDateTime StartTime = FDateTime::Now();
		OnShowTaskStartNotification(TEXT("Starting spatial service..."));

		if (!LocalDeploymentManager->TryStartSpatialService())
		{
			OnShowFailedNotification(TEXT("Spatial service failed to start"));
			UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Could not start spatial service."));
			return;
		}

		FTimespan Span = FDateTime::Now() - StartTime;

		OnShowSuccessNotification(TEXT("Spatial service started!"));
		UE_LOG(LogSpatialGDKEditorToolbar, Log, TEXT("Spatial service started in %f seconds."), Span.GetTotalSeconds());
	});
}

void FSpatialGDKEditorToolbarModule::StopSpatialServiceButtonClicked()
{
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this]
	{
		FDateTime StartTime = FDateTime::Now();
		OnShowTaskStartNotification(TEXT("Stopping spatial service..."));

		if (!LocalDeploymentManager->TryStopSpatialService())
		{
			OnShowFailedNotification(TEXT("Spatial service failed to stop"));
			UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Could not stop spatial service."));
			return;
		}

		FTimespan Span = FDateTime::Now() - StartTime;

		OnShowSuccessNotification(TEXT("Spatial service stopped!"));
		UE_LOG(LogSpatialGDKEditorToolbar, Log, TEXT("Spatial service stopped in %f secoonds."), Span.GetTotalSeconds());
	});
}

void FSpatialGDKEditorToolbarModule::VerifyAndStartDeployment()
{
	// Don't try and start a local deployment if spatial networking is disabled.
	if (!GetDefault<UGeneralProjectSettings>()->bSpatialNetworking)
	{
		UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Attempted to start a local deployment but spatial networking is disabled."));
		return;
	}

	// Get the latest launch config.
	const USpatialGDKEditorSettings* SpatialGDKSettings = GetDefault<USpatialGDKEditorSettings>();

	FString LaunchConfig;
	if (SpatialGDKSettings->bGenerateDefaultLaunchConfig)
	{
		if (!ValidateGeneratedLaunchConfig())
		{
			return;
		}

		if (!GenerateDefaultWorkerJson())
		{
			return;
		}

		LaunchConfig = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir()), TEXT("Improbable/DefaultLaunchConfig.json"));
		GenerateDefaultLaunchConfig(LaunchConfig);
	}
	else
	{
		LaunchConfig = SpatialGDKSettings->GetSpatialOSLaunchConfig();
	}

	const FString LaunchFlags = SpatialGDKSettings->GetSpatialOSCommandLineLaunchFlags();

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, LaunchConfig, LaunchFlags]
	{
		// If the last local deployment is still stopping then wait until it's finished.
		while (LocalDeploymentManager->IsDeploymentStopping())
		{
			FPlatformProcess::Sleep(0.1f);
		}

		// If schema or worker configurations have been changed then we must restart the deployment.
		if (LocalDeploymentManager->IsRedeployRequired() && LocalDeploymentManager->IsLocalDeploymentRunning())
		{
			UE_LOG(LogSpatialGDKEditorToolbar, Display, TEXT("Local deployment must restart."));
			OnShowTaskStartNotification(TEXT("Local deployment restarting.")); 
			LocalDeploymentManager->TryStopLocalDeployment();
		}
		else if (LocalDeploymentManager->IsLocalDeploymentRunning())
		{
			// A good local deployment is already running.
			return;
		}

		OnShowTaskStartNotification(TEXT("Starting local deployment..."));
		if (LocalDeploymentManager->TryStartLocalDeployment(LaunchConfig, LaunchFlags))
		{
			OnShowSuccessNotification(TEXT("Local deployment started!"));
		}
		else
		{
			OnShowFailedNotification(TEXT("Local deployment failed to start"));
		}
	});
}

void FSpatialGDKEditorToolbarModule::StartSpatialDeploymentButtonClicked()
{
	VerifyAndStartDeployment();
}

void FSpatialGDKEditorToolbarModule::StopSpatialDeploymentButtonClicked()
{
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this]
	{
		OnShowTaskStartNotification(TEXT("Stopping local deployment..."));
		if (LocalDeploymentManager->TryStopLocalDeployment())
		{
			OnShowSuccessNotification(TEXT("Successfully stopped local deployment"));
		}
		else
		{
			OnShowFailedNotification(TEXT("Failed to stop local deployment!"));
		}
	});	
}

void FSpatialGDKEditorToolbarModule::LaunchInspectorWebpageButtonClicked()
{
	FString WebError;
	FPlatformProcess::LaunchURL(TEXT("http://localhost:21000/inspector"), TEXT(""), &WebError);
	if (!WebError.IsEmpty())
	{
		FNotificationInfo Info(FText::FromString(WebError));
		Info.ExpireDuration = 3.0f;
		Info.bUseSuccessFailIcons = true;
		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
		NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
		NotificationItem->ExpireAndFadeout();
	}
}

bool FSpatialGDKEditorToolbarModule::StartSpatialDeploymentIsVisible() const
{
	if (LocalDeploymentManager->IsSpatialServiceRunning())
	{
		return !LocalDeploymentManager->IsLocalDeploymentRunning();
	}
	else
	{
		return true;
	}
}

bool FSpatialGDKEditorToolbarModule::StartSpatialDeploymentCanExecute() const
{
	return !LocalDeploymentManager->IsDeploymentStarting() && GetDefault<UGeneralProjectSettings>()->bSpatialNetworking && !CookMapProcess.IsValid();
}

bool FSpatialGDKEditorToolbarModule::StopSpatialDeploymentIsVisible() const
{
	return LocalDeploymentManager->IsSpatialServiceRunning() && LocalDeploymentManager->IsLocalDeploymentRunning();
}

bool FSpatialGDKEditorToolbarModule::StopSpatialDeploymentCanExecute() const
{
	return !LocalDeploymentManager->IsDeploymentStopping() && !CookMapProcess.IsValid();
}

bool FSpatialGDKEditorToolbarModule::StartSpatialServiceIsVisible() const
{
	const USpatialGDKEditorSettings* SpatialGDKSettings = GetDefault<USpatialGDKEditorSettings>();

	return SpatialGDKSettings->bShowSpatialServiceButton && !LocalDeploymentManager->IsSpatialServiceRunning();
}

bool FSpatialGDKEditorToolbarModule::StartSpatialServiceCanExecute() const
{
	return !LocalDeploymentManager->IsServiceStarting();
}

bool FSpatialGDKEditorToolbarModule::StopSpatialServiceIsVisible() const
{
	const USpatialGDKEditorSettings* SpatialGDKSettings = GetDefault<USpatialGDKEditorSettings>();

	return SpatialGDKSettings->bShowSpatialServiceButton && LocalDeploymentManager->IsSpatialServiceRunning();
}

bool FSpatialGDKEditorToolbarModule::StopSpatialServiceCanExecute() const
{
	return !LocalDeploymentManager->IsServiceStopping();
}

/**
* This function is used to update our own local copy of bStopSpatialOnExit as Settings change.
* We keep the copy of the variable as all the USpatialGDKEditorSettings references get
* cleaned before all the available callbacks that IModuleInterface exposes. This means that we can't access
* this variable through its references after the engine is closed.
*/
void FSpatialGDKEditorToolbarModule::OnPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (USpatialGDKEditorSettings* Settings = Cast<USpatialGDKEditorSettings>(ObjectBeingModified))
	{
		FName PropertyName = PropertyChangedEvent.Property != nullptr
				? PropertyChangedEvent.Property->GetFName()
				: NAME_None;
		if (PropertyName.ToString() == TEXT("bStopSpatialOnExit"))
		{
			bStopSpatialOnExit = Settings->bStopSpatialOnExit;
		}
		else if (PropertyName.ToString() == TEXT("bAutoStartLocalDeployment"))
		{
			// TODO: UNR-1776 Workaround for SpatialNetDriver requiring editor settings.
			LocalDeploymentManager->SetAutoDeploy(Settings->bAutoStartLocalDeployment);

			if (Settings->bAutoStartLocalDeployment)
			{
				// Bind the TryStartSpatialDeployment delegate if autostart is enabled.
				UEditorEngine::TryStartSpatialDeployment.BindLambda([this]
				{
					VerifyAndStartDeployment();
				});
			}
			else
			{
				// Unbind the TryStartSpatialDeployment if autostart is disabled.
				UEditorEngine::TryStartSpatialDeployment.Unbind();
			}
		}
	}
}

void FSpatialGDKEditorToolbarModule::ShowSimulatedPlayerDeploymentDialog()
{
	// Create and open the cloud configuration dialog
	SimulatedPlayerDeploymentWindowPtr = SNew(SWindow)
		.Title(LOCTEXT("SimulatedPlayerConfigurationTitle", "Cloud Deployment"))
		.HasCloseButton(true)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::Autosized);

	SimulatedPlayerDeploymentWindowPtr->SetContent(
		SNew(SBox)
		.WidthOverride(700.0f)
		[
			SAssignNew(SimulatedPlayerDeploymentConfigPtr, SSpatialGDKSimulatedPlayerDeployment)
			.SpatialGDKEditor(SpatialGDKEditorInstance)
			.ParentWindow(SimulatedPlayerDeploymentWindowPtr)
		]
	);

	TSharedPtr<SWindow> RootWindow = FGlobalTabmanager::Get()->GetRootWindow();

	FSlateApplication::Get().AddModalWindow(SimulatedPlayerDeploymentWindowPtr.ToSharedRef(), RootWindow);
}

bool FSpatialGDKEditorToolbarModule::GenerateDefaultLaunchConfig(const FString& LaunchConfigPath) const
{
	if (const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>())
	{
		FString Text;
		TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&Text);

		const FSpatialLaunchConfigDescription& LaunchConfigDescription = SpatialGDKEditorSettings->LaunchConfigDesc;

		// Populate json file for launch config
		Writer->WriteObjectStart(); // Start of json
			Writer->WriteValue(TEXT("template"), LaunchConfigDescription.Template); // Template section
			Writer->WriteObjectStart(TEXT("world")); // World section begin
				Writer->WriteObjectStart(TEXT("dimensions"));
					Writer->WriteValue(TEXT("x_meters"), LaunchConfigDescription.World.Dimensions.X);
					Writer->WriteValue(TEXT("z_meters"), LaunchConfigDescription.World.Dimensions.Y);
				Writer->WriteObjectEnd();
			Writer->WriteValue(TEXT("chunk_edge_length_meters"), LaunchConfigDescription.World.ChunkEdgeLengthMeters);
			Writer->WriteArrayStart(TEXT("legacy_flags"));
			for (auto& Flag : LaunchConfigDescription.World.LegacyFlags)
			{
				WriteFlagSection(Writer, Flag.Key, Flag.Value);
			}
			Writer->WriteArrayEnd();
			Writer->WriteArrayStart(TEXT("legacy_javaparams"));
			for (auto& Parameter : LaunchConfigDescription.World.LegacyJavaParams)
			{
				WriteFlagSection(Writer, Parameter.Key, Parameter.Value);
			}
			Writer->WriteArrayEnd();
			Writer->WriteObjectStart(TEXT("snapshots"));
				Writer->WriteValue(TEXT("snapshot_write_period_seconds"), LaunchConfigDescription.World.SnapshotWritePeriodSeconds);
			Writer->WriteObjectEnd();
		Writer->WriteObjectEnd(); // World section end
		Writer->WriteObjectStart(TEXT("load_balancing")); // Load balancing section begin
			Writer->WriteArrayStart("layer_configurations");
			for (const FWorkerTypeLaunchSection& Worker : LaunchConfigDescription.ServerWorkers)
			{
				WriteLoadbalancingSection(Writer, Worker.WorkerTypeName, Worker.Columns, Worker.Rows, Worker.bManualWorkerConnectionOnly);
			}
			Writer->WriteArrayEnd();
			Writer->WriteObjectEnd(); // Load balancing section end
			Writer->WriteArrayStart(TEXT("workers")); // Workers section begin
			for (const FWorkerTypeLaunchSection& Worker : LaunchConfigDescription.ServerWorkers)
			{
				WriteWorkerSection(Writer, Worker);
			}
			// Write the client worker section
			FWorkerTypeLaunchSection ClientWorker;
			ClientWorker.WorkerTypeName = SpatialConstants::DefaultClientWorkerType;
			ClientWorker.WorkerPermissions.bAllPermissions = true;
			ClientWorker.bLoginRateLimitEnabled = false;
			WriteWorkerSection(Writer, ClientWorker);
			Writer->WriteArrayEnd(); // Worker section end
		Writer->WriteObjectEnd(); // End of json

		Writer->Close();

		if (!FFileHelper::SaveStringToFile(Text, *LaunchConfigPath))
		{
			UE_LOG(LogSpatialGDKEditorToolbar, Log, TEXT("Failed to write output file '%s'. It might be that the file is read-only."), *LaunchConfigPath);
			return false;
		}

		return true;
	}

	return false;
}

bool FSpatialGDKEditorToolbarModule::GenerateDefaultWorkerJson()
{
	if (const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>())
	{
		const FString WorkerJsonDir = FSpatialGDKServicesModule::GetSpatialOSDirectory(TEXT("workers/unreal"));
		const FString TemplateWorkerJsonPath = FSpatialGDKServicesModule::GetSpatialGDKPluginDirectory(TEXT("SpatialGDK/Extras/templates/WorkerJsonTemplate.json"));

		const FSpatialLaunchConfigDescription& LaunchConfigDescription = SpatialGDKEditorSettings->LaunchConfigDesc;
		for (const FWorkerTypeLaunchSection& Worker : LaunchConfigDescription.ServerWorkers)
		{
			FString JsonPath = FPaths::Combine(WorkerJsonDir, FString::Printf(TEXT("spatialos.%s.worker.json"), *Worker.WorkerTypeName.ToString()));
			if (!FPaths::FileExists(JsonPath))
			{
				UE_LOG(LogSpatialGDKEditorToolbar, Verbose, TEXT("Could not find worker json at %s"), *JsonPath);
				FString Contents;
				if (FFileHelper::LoadFileToString(Contents, *TemplateWorkerJsonPath))
				{
					Contents.ReplaceInline(TEXT("{{WorkerTypeName}}"), *Worker.WorkerTypeName.ToString());
					if (FFileHelper::SaveStringToFile(Contents, *JsonPath))
					{
						LocalDeploymentManager->SetRedeployRequired();
						UE_LOG(LogSpatialGDKEditorToolbar, Verbose, TEXT("Wrote default worker json to %s"), *JsonPath)
					}
					else
					{
						UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Failed to write default worker json to %s"), *JsonPath)
					}
				}
				else
				{
					UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Failed to read default worker json template at %s"), *TemplateWorkerJsonPath)
				}
			}
			else
			{
				UE_LOG(LogSpatialGDKEditorToolbar, Verbose, TEXT("Found worker json at %s"), *JsonPath)
			}
		}

		return true;
	}

	return false;
}

void FSpatialGDKEditorToolbarModule::GenerateSchema(bool bFullScan)
{
	LocalDeploymentManager->SetRedeployRequired();

	if (SpatialGDKEditorInstance->FullScanRequired())
	{
		OnShowTaskStartNotification("Initial Schema Generation");

		if (SpatialGDKEditorInstance->GenerateSchema(true))
		{
			OnShowSuccessNotification("Initial Schema Generation completed!");
		}
		else
		{
			OnShowFailedNotification("Initial Schema Generation failed");
		}
	}
	else if (bFullScan)
	{
		OnShowTaskStartNotification("Generating Schema (Full)");

		if (SpatialGDKEditorInstance->GenerateSchema(true))
		{
			OnShowSuccessNotification("Full Schema Generation completed!");
		}
		else
		{
			OnShowFailedNotification("Full Schema Generation failed");
		}
	}
	else
	{
		OnShowTaskStartNotification("Generating Schema (Incremental)");

		if (SpatialGDKEditorInstance->GenerateSchema(false))
		{
			OnShowSuccessNotification("Incremental Schema Generation completed!");
		}
		else
		{
			OnShowFailedNotification("Incremental Schema Generation failed");
		}
	}
}

bool FSpatialGDKEditorToolbarModule::WriteFlagSection(TSharedRef< TJsonWriter<> > Writer, const FString& Key, const FString& Value) const
{
	Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), Key);
		Writer->WriteValue(TEXT("value"), Value);
	Writer->WriteObjectEnd();

	return true;
}

bool FSpatialGDKEditorToolbarModule::WriteWorkerSection(TSharedRef< TJsonWriter<> > Writer, const FWorkerTypeLaunchSection& Worker) const
{
	Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("worker_type"), *Worker.WorkerTypeName.ToString());
		Writer->WriteArrayStart(TEXT("flags"));
		for (const auto& Flag : Worker.Flags)
		{
			WriteFlagSection(Writer, Flag.Key, Flag.Value);
		}
		Writer->WriteArrayEnd();
		Writer->WriteArrayStart(TEXT("permissions"));
			Writer->WriteObjectStart();
			if (Worker.WorkerPermissions.bAllPermissions)
			{
				Writer->WriteObjectStart(TEXT("all"));
				Writer->WriteObjectEnd();
			}
			else
			{
				Writer->WriteObjectStart(TEXT("entity_creation"));
					Writer->WriteValue(TEXT("allow"), Worker.WorkerPermissions.bAllowEntityCreation);
				Writer->WriteObjectEnd();
				Writer->WriteObjectStart(TEXT("entity_deletion"));
					Writer->WriteValue(TEXT("allow"), Worker.WorkerPermissions.bAllowEntityDeletion);
				Writer->WriteObjectEnd();
				Writer->WriteObjectStart(TEXT("entity_query"));
					Writer->WriteValue(TEXT("allow"), Worker.WorkerPermissions.bAllowEntityQuery);
					Writer->WriteArrayStart("components");
					for (const FString& Component : Worker.WorkerPermissions.Components)
					{
						Writer->WriteValue(Component);
					}
					Writer->WriteArrayEnd();
				Writer->WriteObjectEnd();
			}
			Writer->WriteObjectEnd();
		Writer->WriteArrayEnd();
		if (Worker.MaxConnectionCapacityLimit > 0)
		{
			Writer->WriteObjectStart(TEXT("connection_capacity_limit"));
				Writer->WriteValue(TEXT("max_capacity"), Worker.MaxConnectionCapacityLimit);
			Writer->WriteObjectEnd();
		}
		if (Worker.bLoginRateLimitEnabled)
		{
			Writer->WriteObjectStart(TEXT("login_rate_limit"));
				Writer->WriteValue(TEXT("duration"), Worker.LoginRateLimit.Duration);
				Writer->WriteValue(TEXT("requests_per_duration"), Worker.LoginRateLimit.RequestsPerDuration);
			Writer->WriteObjectEnd();
		}
	Writer->WriteObjectEnd();

	return true;
}

bool FSpatialGDKEditorToolbarModule::WriteLoadbalancingSection(TSharedRef< TJsonWriter<> > Writer, const FName& WorkerType, const int32 Columns, const int32 Rows, const bool ManualWorkerConnectionOnly) const
{
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("layer"), *WorkerType.ToString());
		Writer->WriteObjectStart("rectangle_grid");
			Writer->WriteValue(TEXT("cols"), Columns);
			Writer->WriteValue(TEXT("rows"), Rows);
		Writer->WriteObjectEnd();
		Writer->WriteObjectStart(TEXT("options"));
			Writer->WriteValue(TEXT("manual_worker_connection_only"), ManualWorkerConnectionOnly);
		Writer->WriteObjectEnd();
	Writer->WriteObjectEnd();

	return true;
}

// CORVUS_BEGIN
void FSpatialGDKEditorToolbarModule::LoadSublevels()
{
	// Display a notification an add a delay to let it shows up since the snapshot can stall the editor for a while
	UnrealUtils::DisplayNotification(TEXT("Loading sublevels..."));
	UnrealUtils::DelayedExecUnsafe(*GEditor->GetTimerManager(), 0.5f, [this]() {
		// In World Composition, we need to load all sub-levels to populate the snapshot
		UnrealUtils::LoadWorldCompositionStreamingLevels();
	});
}

bool FSpatialGDKEditorToolbarModule::CanLoadSublevels() const
{
	// Can package networked client only if cooking is not running
	// Can package networked client only if packaging not already in progress
	// Can package networked client only if SpatialOS not already running
	return !CookMapProcess.IsValid() && !PackageClientProcess.IsValid() && !LocalDeploymentManager->IsLocalDeploymentRunning();
}

FText FSpatialGDKEditorToolbarModule::LoadSublevelsTooltip() const
{
	if (CookMapProcess.IsValid())
		return LOCTEXT("CookMapRunning_Tooltip", "Cooking map in progress.");
	else if (PackageClientProcess.IsValid())
		return LOCTEXT("PackageNetworkedClientRunning_Tooltip", "Packaging client in progress.");
	else
		return LOCTEXT("LoadSublevels_Tooltip", "Load distance-dependent streaming levels (excluding those with streaming disabled).\nRequired before generating schema if the map uses World Composition.");
}

void FSpatialGDKEditorToolbarModule::CookMap()
{
	// Prompt to save or discard all packages
	if (!UnrealUtils::PromptUserToSaveDirtyPackages())
	{
		UnrealUtils::DisplayNotification(TEXT("Unsaved Content. You need to save all before Cooking."), false);
		return;
	}

	const FString AutomationTool = FPaths::RootDir() / TEXT("Engine/Binaries/DotNET/AutomationTool.exe");
	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(GEditor->GetEditorWorldContext().World());
	const FString ProjectFilePath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString CommandLineArgs = FString::Printf(TEXT("BuildCookRun -project=\"%s\" -unattended -noP4 -Map=%s -clientconfig=Development -platform=Win64 -server -serverconfig=Development -serverplatform=Win64 -nocompile -nocompileeditor -cook -iterativecooking -SkipCookingEditorContent -unversioned"), *ProjectFilePath, *CurrentLevelName);
	CookMapProcess = UnrealUtils::LaunchMonitoredProcess(TEXT("Cooking Map"), AutomationTool, CommandLineArgs);
}

bool FSpatialGDKEditorToolbarModule::CanCookMap() const
{
	// Can launch cooking only if not already running
	// Can launch cooking only if SpatialOS not already running (because Cook Map is somehow killing any running SpatialOS)
	// Can launch cooking only if dedicated server not already running
	// TODO: we should check for network client(s) (at least the first one)
	return !CookMapProcess.IsValid() && !LocalDeploymentManager->IsLocalDeploymentRunning() && !ServerProcessHandle.IsValid();
}

FText FSpatialGDKEditorToolbarModule::CookMapTooltip() const
{
	if (CookMapProcess.IsValid())
		return LOCTEXT("CookMapRunning_Tooltip", "Cooking map in progress.");
	else if (ServerProcessHandle.IsValid())
		return LOCTEXT("CookMapWorkerRunning_Tooltip", "Cannot cook map with server or client running.");
	else
		return LOCTEXT("CookMap_Tooltip", "Cook all data related to the current map.\nRequired to launch a dedicated server or networked client.");
}

void FSpatialGDKEditorToolbarModule::LaunchDedicatedServer()
{
	static const FString WorkingDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	static const FString ServerPath = WorkingDir / TEXT("Binaries/Win64/CorvusServer.exe");
	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(GEditor->GetEditorWorldContext().World());
	const USpatialGDKEditorSettings* Settings = GetDefault<USpatialGDKEditorSettings>();
	const FString CommandLineFlags = Settings->LocalWorflowServerCommandLineFlags;

	// Launch the real dedicated server executable
	const FString CommandLineArgs = FString::Printf(
		TEXT("%s +appName corvus +projectName corvus +deploymentName corvus_local_workflow +workerType UnrealWorker +useExternalIpForBridge true -messaging -SessionName=\"Local Workflow UnrealWorker Server\" -log -nopauseonsuccess -NoVerifyGC %s"),
		*CurrentLevelName, *CommandLineFlags);

	ServerProcessHandle = UnrealUtils::LaunchProcess(ServerPath, CommandLineArgs, WorkingDir);

	UnrealUtils::DisplayNotification(ServerProcessHandle.IsValid()
		? FString::Printf(TEXT("Dedicated Server Starting on %s..."), *CurrentLevelName)
		: TEXT("Failed to start Dedicated Server"),
		ServerProcessHandle.IsValid());
}

bool FSpatialGDKEditorToolbarModule::CanLaunchDedicatedServer() const
{
	// Can launch dedicated server only if cooking is not running
	// Can launch dedicated server only if SpatialOS is running
	// Can launch dedicated server only if not already running
	return !CookMapProcess.IsValid() && LocalDeploymentManager->IsLocalDeploymentRunning() && !ServerProcessHandle.IsValid();
}

FText FSpatialGDKEditorToolbarModule::LaunchDedicatedServerTooltip() const
{
	if (CookMapProcess.IsValid())
		return LOCTEXT("CookMapRunning_Tooltip", "Cooking map in progress.");
	else if (!LocalDeploymentManager->IsLocalDeploymentRunning())
		return LOCTEXT("SpatialOsNotRunning_Tooltip", "SpatialOS is not running.");
	else if (ServerProcessHandle.IsValid())
		return LOCTEXT("DedicatedServerRunning_Tooltip", "Dedicated Server is running.");
	else
		return LOCTEXT("LaunchDedicatedServer_Tooltip", "Launch a dedicated server worker.\nUses the final server executable in a new process.");
}

void FSpatialGDKEditorToolbarModule::LaunchNetworkedClient()
{
	static const FString WorkingDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	static const FString ClientPath = WorkingDir / TEXT("Binaries/Win64/Corvus.exe");
	const USpatialGDKEditorSettings* Settings = GetDefault<USpatialGDKEditorSettings>();
	const FString ServerIpAddr = Settings->LocalWorflowServerIpAddr.IsEmpty() ? TEXT("127.0.0.1") : Settings->LocalWorflowServerIpAddr;
	const FString CommandLineFlags = Settings->LocalWorflowClientCommandLineFlags;

	const FString CommandLineArgs = FString::Printf(
		TEXT("%s +appName corvus +projectName corvus +deploymentName corvus_local_workflow +workerType UnrealClient +useExternalIpForBridge true -messaging -SessionName=\"Local Workflow UnrealClient\" -log -NoVerifyGC %s"),
		*ServerIpAddr, *CommandLineFlags);

	FProcHandle clientProcessHandle = UnrealUtils::LaunchProcess(ClientPath, CommandLineArgs, WorkingDir);

	UnrealUtils::DisplayNotification(clientProcessHandle.IsValid()
		? FString::Printf(TEXT("Network Client Starting on %s..."), *ServerIpAddr)
		: TEXT("Failed to start Network Client"),
		clientProcessHandle.IsValid());
	FPlatformProcess::CloseProc(clientProcessHandle);
}

bool FSpatialGDKEditorToolbarModule::CanLaunchNetworkedClient() const
{
	// Can launch networked client only if cooking is not running
	// Can launch networked client only if SpatialOS is running
	// Can launch networked client only with the dedicated server running
	return !CookMapProcess.IsValid() && LocalDeploymentManager->IsLocalDeploymentRunning() && ServerProcessHandle.IsValid();
}

FText FSpatialGDKEditorToolbarModule::LaunchNetworkedClientTooltip() const
{
	if (CookMapProcess.IsValid())
		return LOCTEXT("CookMapRunning_Tooltip", "Cooking map in progress.");
	else if (!LocalDeploymentManager->IsLocalDeploymentRunning())
		return LOCTEXT("SpatialOsNotRunning_Tooltip", "SpatialOS is not running.");
	else if (!ServerProcessHandle.IsValid())
		return LOCTEXT("DedicatedServerNotRunning_Tooltip", "Dedicated server is not running.");
	else
		return LOCTEXT("LaunchNetworkedClient_Tooltip", "Launch a networked client.\nUses the final game client executable in a new process.");
}

void FSpatialGDKEditorToolbarModule::ExploreDedicatedServerLogs() const
{
	static const FString SavedPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	const FString LogsPath = FPaths::Combine(SavedPath, TEXT("Cooked/WindowsServer/Corvus/Saved/Logs/"));
	FPlatformProcess::ExploreFolder(*LogsPath);
}

void FSpatialGDKEditorToolbarModule::ExploreNetworkedClientLogs()
{
	static const FString SavedPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	const FString LogsPath = FPaths::Combine(SavedPath, TEXT("Cooked/WindowsNoEditor/Corvus/Saved/Logs/"));
	FPlatformProcess::ExploreFolder(*LogsPath);
}

void FSpatialGDKEditorToolbarModule::PackageNetworkedClient()
{
	FString PackageClientBatch = FPaths::RootDir() / TEXT("../Tools/LocalDemo/package_game_client.bat");
	FPaths::CollapseRelativeDirectories(PackageClientBatch);
	const FString CommandLineArgs = FString::Printf(TEXT("/c \"%s\""), *PackageClientBatch);
	if (FPaths::FileExists(PackageClientBatch))
	{
		PackageClientProcess = UnrealUtils::LaunchMonitoredProcess(TEXT("Packaging Client"), TEXT("cmd.exe"), CommandLineArgs);
	}
	else
	{
		UnrealUtils::DisplayNotification(FString::Printf(TEXT("%s not found"), *PackageClientBatch), false);
	}
}

bool FSpatialGDKEditorToolbarModule::CanPackageNetworkedClient() const
{
	// Can package networked client only if cooking is not running
	// Can package networked client only if packaging not already in progress
	return !CookMapProcess.IsValid() && !PackageClientProcess.IsValid();
}

FText FSpatialGDKEditorToolbarModule::PackageNetworkedClientTooltip() const
{
	if (CookMapProcess.IsValid())
		return LOCTEXT("CookMapRunning_Tooltip", "Cooking map in progress.");
	else if (PackageClientProcess.IsValid())
		return LOCTEXT("PackageNetworkedClientRunning_Tooltip", "Packaging client in progress.");
	else
		return LOCTEXT("PackageNetworkedClient_Tooltip", "Package a networked client for remote multi-player game.\nWill connect to the dedicated server at the current IP Address.");
}

// CORVUS_END

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSpatialGDKEditorToolbarModule, SpatialGDKEditorToolbar)
