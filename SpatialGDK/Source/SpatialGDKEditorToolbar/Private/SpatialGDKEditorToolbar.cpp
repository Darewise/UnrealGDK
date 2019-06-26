// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialGDKEditorToolbar.h"
#include "Async/Async.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISettingsContainer.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Misc/MessageDialog.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "SpatialGDKEditorToolbarCommands.h"
#include "SpatialGDKEditorToolbarStyle.h"

#include "SpatialConstants.h"
#include "SpatialGDKEditor.h"
#include "SpatialGDKSettings.h"
#include "SpatialGDKEditorSettings.h"

#include "Editor/EditorEngine.h"
#include "HAL/FileManager.h"
#include "Sound/SoundBase.h"

#include "AssetRegistryModule.h"
#include "GeneralProjectSettings.h"
#include "Kismet/GameplayStatics.h"
#include "LevelEditor.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonWriter.h"

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

	CheckForRunningStack();

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
		StopRunningStack();
	}
}

void FSpatialGDKEditorToolbarModule::Tick(float DeltaTime)
{
	if (SpatialOSStackProcessID != 0 && !FPlatformProcess::IsApplicationRunning(SpatialOSStackProcessID))
	{
		CleanupSpatialProcess();
	}

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
	return SpatialGDKEditorInstance.IsValid() && !SpatialGDKEditorInstance.Get()->IsSchemaGeneratorRunning();
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
		FSpatialGDKEditorToolbarCommands::Get().StartSpatialOSStackAction,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartSpatialOSButtonClicked),
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartSpatialOSStackCanExecute),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartSpatialOSStackCanExecute));

	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().StopSpatialOSStackAction,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StopSpatialOSButtonClicked),
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StopSpatialOSStackCanExecute),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StopSpatialOSStackCanExecute));

	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().LaunchInspectorWebPageAction,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LaunchInspectorWebpageButtonClicked),
		FCanExecuteAction());
}

void FSpatialGDKEditorToolbarModule::SetupToolbar(TSharedPtr<class FUICommandList> InPluginCommands)
{
	FLevelEditorModule& LevelEditorModule =
		FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	{
		/* Corvus: no interest of putting these in menus, especially not in Window menu (should use a dedicated one)
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());
		MenuExtender->AddMenuExtension(
			"General", EExtensionHook::After, InPluginCommands,
			FMenuExtensionDelegate::CreateRaw(this, &FSpatialGDKEditorToolbarModule::AddMenuExtension));

		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
		*/
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
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSchema);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSnapshot);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().StartSpatialOSStackAction);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().StopSpatialOSStackAction);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().LaunchInspectorWebPageAction);
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
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().StartSpatialOSStackAction);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().StopSpatialOSStackAction);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().LaunchInspectorWebPageAction);
	// CORVUS_BEGIN: see the reference "Play combo box" in FPlayWorldCommands::BuildToolbar() to add a menu
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
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, NULL);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("LocalWorkflow", "Local Workflow"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("LoadSublevels", "Load sub-levels"),
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LoadSublevelsTooltip)),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.WorldBrowser"),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LoadSublevels),
				FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanLoadSublevels)
			)
		);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CookMap", "Cook Current Map"),
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CookMapTooltip)),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Build"),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CookMap),
				FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanCookMap)
			)
		);
		// TODO: change icon if using server with rendering
		MenuBuilder.AddMenuEntry(
			LOCTEXT("LaunchDedicatedServer", "Launch Dedicated Server"),
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LaunchDedicatedServerTooltip)),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "PlayWorld.PlayInNewProcess"),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LaunchDedicatedServer),
				FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanLaunchDedicatedServer)
			)
		);
		/* TODO: option to Launch a fake Server with rendering (Game client with the -server option) Not working with the GDK!
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ServerWithRendering", "With rendering"),
			LOCTEXT("ServerWithRendering_Tooltip", "Launch a networked client as a server.\nThis enable rendering but don't test the real server."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::ToggleServerWithRendering),
				FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanLaunchDedicatedServer),
				FIsActionChecked::CreateRaw(this, &FSpatialGDKEditorToolbarModule::IsServerWithRendering)
			),
			NAME_None,	// Extension point
			EUserInterfaceActionType::ToggleButton
		);
		*/

		// TODO: add parameter to specify IP Addr of a remote server
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
	ShowTaskStartNotification("Started snapshot generation");

	const USpatialGDKEditorSettings* Settings = GetDefault<USpatialGDKEditorSettings>();

	FString SnapshotFilename;
	if (Settings->IsSnapshotUsingCurrentLevelName())
	{
		const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(GEditor->GetEditorWorldContext().World());
		SnapshotFilename = CurrentLevelName + TEXT(".snapshot");
	}
	else
	{
		SnapshotFilename = Settings->GetSpatialOSSnapshotFile();
	}
	SpatialGDKEditorInstance->GenerateSnapshot(
		GEditor->GetEditorWorldContext().World(), SnapshotFilename,
		FSimpleDelegate::CreateLambda([this]() { ShowSuccessNotification("Snapshot successfully generated!"); }),
		FSimpleDelegate::CreateLambda([this]() { ShowFailedNotification("Snapshot generation failed!"); }),
		FSpatialGDKEditorErrorHandler::CreateLambda([](FString ErrorText) { FMessageDialog::Debugf(FText::FromString(ErrorText)); }));
}

void FSpatialGDKEditorToolbarModule::SchemaGenerateButtonClicked()
{
	ShowTaskStartNotification("Generating Schema (Incremental)");

	if (SpatialGDKEditorInstance->GenerateSchema(false))
	{
		ShowSuccessNotification("Incremental Schema Generation completed!");
	}
	else
	{
		ShowFailedNotification("Incremental Schema Generation failed");
	}
}

void FSpatialGDKEditorToolbarModule::SchemaGenerateFullButtonClicked()
{
	ShowTaskStartNotification("Generating Schema (Full)");

	if (SpatialGDKEditorInstance->GenerateSchema(true))
	{
		ShowSuccessNotification("Full Schema Generation completed!");
	}
	else
	{
		ShowFailedNotification("Full Schema Generation failed");
	}
}
		

void FSpatialGDKEditorToolbarModule::ShowTaskStartNotification(const FString& NotificationText)
{
	if (TaskNotificationPtr.IsValid())
	{
		TaskNotificationPtr.Pin()->ExpireAndFadeout();
	}

	if (GEditor && ExecutionStartSound)
	{
		GEditor->PlayEditorSound(ExecutionStartSound);
	}

	FNotificationInfo Info(FText::AsCultureInvariant(NotificationText));
	Info.Image = FEditorStyle::GetBrush(TEXT("LevelEditor.RecompileGameCode"));
	Info.ExpireDuration = 5.0f;
	Info.bFireAndForget = false;

	TaskNotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);

	if (TaskNotificationPtr.IsValid())
	{
		TaskNotificationPtr.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FSpatialGDKEditorToolbarModule::ShowSuccessNotification(const FString& NotificationText)
{
	AsyncTask(ENamedThreads::GameThread, [this, NotificationText]{
		TSharedPtr<SNotificationItem> Notification = TaskNotificationPtr.Pin();
		Notification->SetFadeInDuration(0.1f);
		Notification->SetFadeOutDuration(0.5f);
		Notification->SetExpireDuration(7.5f);
		Notification->SetText(FText::AsCultureInvariant(NotificationText));
		Notification->SetCompletionState(SNotificationItem::CS_Success);
		Notification->ExpireAndFadeout();
		TaskNotificationPtr.Reset();

		if (GEditor && ExecutionSuccessSound)
		{
			GEditor->PlayEditorSound(ExecutionSuccessSound);
		}
	});
}

void FSpatialGDKEditorToolbarModule::ShowFailedNotification(const FString& NotificationText)
{
	AsyncTask(ENamedThreads::GameThread, [this, NotificationText]{
		TSharedPtr<SNotificationItem> Notification = TaskNotificationPtr.Pin();
		Notification->SetText(FText::AsCultureInvariant(NotificationText));
		Notification->SetCompletionState(SNotificationItem::CS_Fail);
		Notification->SetExpireDuration(5.0f);

		Notification->ExpireAndFadeout();

		if (GEditor && ExecutionFailSound)
		{
			GEditor->PlayEditorSound(ExecutionFailSound);
		}
	});
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

	int32 PlayNumberOfServers;
	GetDefault<ULevelEditorPlaySettings>()->GetPlayNumberOfServers(PlayNumberOfServers);
	if (!SpatialGDKRuntimeSettings->bEnableHandover && PlayNumberOfServers > 1)
	{
		const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("Property handover is disabled and multiple launch servers are specified.\nThis is not supported.\n\nDo you want to configure your project settings now?")));

		if (Result == EAppReturnType::Yes)
		{
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Runtime Settings");
		}

		return false;
	}

	return true;
}

void FSpatialGDKEditorToolbarModule::StartSpatialOSButtonClicked()
{
	const USpatialGDKEditorSettings* SpatialGDKSettings = GetDefault<USpatialGDKEditorSettings>();

	FString LaunchConfig;
	if (SpatialGDKSettings->bGenerateDefaultLaunchConfig)
	{
		if (!ValidateGeneratedLaunchConfig())
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

	const FString ExecuteAbsolutePath = SpatialGDKSettings->GetSpatialOSDirectory();
	const FString CmdExecutable = TEXT("cmd.exe");
	// Launch with the snapshot named like the current level
	FString SnapshotFilename;
	if (SpatialGDKSettings->IsSnapshotUsingCurrentLevelName())
	{
		const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(GEditor->GetEditorWorldContext().World());
		SnapshotFilename = CurrentLevelName + TEXT(".snapshot");
	}
	else
	{
		SnapshotFilename = SpatialGDKSettings->GetSpatialOSSnapshotFile();
	}
	const FString SnapshotFilePath = FPaths::Combine(SpatialGDKSettings->GetSpatialOSSnapshotFolderPath(), SnapshotFilename);

	const FString SpatialCmdArgument = FString::Printf(
		TEXT("/c cmd.exe /c spatial.exe update ^& spatial.exe worker build build-config ^& spatial.exe local launch \"%s\" --snapshot=%s %s ^& pause"), *LaunchConfig, *SnapshotFilePath, *SpatialGDKSettings->GetSpatialOSCommandLineLaunchFlags());

	UE_LOG(LogSpatialGDKEditorToolbar, Log, TEXT("Starting cmd.exe with `%s` arguments."), *SpatialCmdArgument);
	// Temporary workaround: To get spatial.exe to properly show a window we have to call cmd.exe to
	// execute it. We currently can't use pipes to capture output as it doesn't work properly with current
	// spatial.exe.
	SpatialOSStackProcHandle = FPlatformProcess::CreateProc(
		*(CmdExecutable), *SpatialCmdArgument, true, false, false, nullptr, 0,
		*ExecuteAbsolutePath, nullptr, nullptr);

	FNotificationInfo Info(SpatialOSStackProcHandle.IsValid() == true
							 ? FText::FromString(TEXT("SpatialOS Starting..."))
							 : FText::FromString(TEXT("Failed to start SpatialOS")));
	Info.ExpireDuration = 3.0f;
	Info.bUseSuccessFailIcons = true;
	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

	if (!SpatialOSStackProcHandle.IsValid())
	{
		NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
		const FString SpatialLogPath =
			SpatialGDKSettings->GetSpatialOSDirectory() + FString(TEXT("/logs/spatial.log"));
		UE_LOG(LogSpatialGDKEditorToolbar, Error,
				TEXT("Failed to start SpatialOS, please refer to log file `%s` for more information."),
				*SpatialLogPath);
	}
	else
	{
		NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
	}

	NotificationItem->ExpireAndFadeout();
}

void FSpatialGDKEditorToolbarModule::StopSpatialOSButtonClicked()
{
	StopRunningStack();
}

void FSpatialGDKEditorToolbarModule::StopRunningStack()
{
	if (SpatialOSStackProcHandle.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(SpatialOSStackProcHandle))
		{
			FPlatformProcess::TerminateProc(SpatialOSStackProcHandle, true);
		}

		CleanupSpatialProcess();
	}
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

bool FSpatialGDKEditorToolbarModule::StartSpatialOSStackCanExecute() const
{
	return !SpatialOSStackProcHandle.IsValid() && !FPlatformProcess::IsProcRunning(const_cast<FProcHandle&>(SpatialOSStackProcHandle));
}

bool FSpatialGDKEditorToolbarModule::StopSpatialOSStackCanExecute() const
{
	return SpatialOSStackProcHandle.IsValid();
}

void FSpatialGDKEditorToolbarModule::CheckForRunningStack()
{
	FPlatformProcess::FProcEnumerator ProcEnumerator;
	do
	{
		FPlatformProcess::FProcEnumInfo Proc = ProcEnumerator.GetCurrent();
		const FString ProcName = Proc.GetName();
		if (ProcName.Compare(TEXT("spatial.exe"), ESearchCase::IgnoreCase) == 0)
		{
			uint32 ProcPID = Proc.GetPID();
			SpatialOSStackProcHandle = FPlatformProcess::OpenProcess(ProcPID);
		}
	} while (ProcEnumerator.MoveNext() && !SpatialOSStackProcHandle.IsValid());
}

void FSpatialGDKEditorToolbarModule::CleanupSpatialProcess()
{
	FPlatformProcess::CloseProc(SpatialOSStackProcHandle);
	SpatialOSStackProcessID = 0;

	OnSpatialShutdown.Broadcast();
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
	}
}

bool FSpatialGDKEditorToolbarModule::GenerateDefaultLaunchConfig(const FString& LaunchConfigPath) const
{
	if (const USpatialGDKEditorSettings* SpatialGDKSettings = GetDefault<USpatialGDKEditorSettings>())
	{
		FString Text;
		TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&Text);

		const FSpatialLaunchConfigDescription& LaunchConfigDescription = SpatialGDKSettings->LaunchConfigDesc;

		// Populate json file for launch config
		Writer->WriteObjectStart(); // Start of json
			Writer->WriteValue(TEXT("template"), LaunchConfigDescription.Template); // Template section
			Writer->WriteObjectStart(TEXT("world")); // World section begin
				Writer->WriteObjectStart(TEXT("dimensions"));
					Writer->WriteValue(TEXT("x_meters"), LaunchConfigDescription.World.Dimensions.X);
					Writer->WriteValue(TEXT("z_meters"), LaunchConfigDescription.World.Dimensions.Y);
				Writer->WriteObjectEnd();
			Writer->WriteValue(TEXT("chunk_edge_length_meters"), LaunchConfigDescription.World.ChunkEdgeLengthMeters);
			Writer->WriteValue(TEXT("streaming_query_interval"), LaunchConfigDescription.World.StreamingQueryIntervalSeconds);
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
			for (const FWorkerTypeLaunchSection& Worker : LaunchConfigDescription.Workers)
			{
				WriteLoadbalancingSection(Writer, Worker.WorkerTypeName, Worker.Columns, Worker.Rows, Worker.bManualWorkerConnectionOnly);
			}
			Writer->WriteArrayEnd();
			Writer->WriteObjectEnd(); // Load balancing section end
			Writer->WriteArrayStart(TEXT("workers")); // Workers section begin
			for (const FWorkerTypeLaunchSection& Worker : LaunchConfigDescription.Workers)
			{
				WriteWorkerSection(Writer, Worker);
			}
			// Write the client worker section
			FWorkerTypeLaunchSection ClientWorker;
			ClientWorker.WorkerTypeName = SpatialConstants::ClientWorkerType;
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
		Writer->WriteValue(TEXT("worker_type"), *Worker.WorkerTypeName);
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

bool FSpatialGDKEditorToolbarModule::WriteLoadbalancingSection(TSharedRef< TJsonWriter<> > Writer, const FString& WorkerType, const int32 Columns, const int32 Rows, const bool ManualWorkerConnectionOnly) const
{
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("layer"), WorkerType);
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
	UnrealUtils::DelayedExec(*GEditor->GetTimerManager(), 0.5f, [this]() {
		// In World Composition, we need to load all sub-levels to populate the snapshot
		UnrealUtils::LoadWorldCompositionStreamingLevels();
	});
}

bool FSpatialGDKEditorToolbarModule::CanLoadSublevels() const
{
	// Can package networked client only if cooking is not running
	// Can package networked client only if packaging not already in progress
	return !CookMapProcess.IsValid() && !PackageClientProcess.IsValid();
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

	// TODO: call directly UAT here instead of relying on a batch file
	FString cookMapBatch = FPaths::RootDir() / TEXT("../Tools/LocalDemo/cook_map.bat");
	FPaths::CollapseRelativeDirectories(cookMapBatch);
	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(GEditor->GetEditorWorldContext().World());
	const FString cmdArgs = FString::Printf(TEXT("/c \"\"%s\" %s\""), *cookMapBatch, *CurrentLevelName);
	if (FPaths::FileExists(cookMapBatch))
	{
		CookMapProcess = UnrealUtils::LaunchMonitoredProcess(TEXT("Cooking Map"), TEXT("cmd.exe"), cmdArgs);
	}
	else
	{
		UnrealUtils::DisplayNotification(FString::Printf(TEXT("%s not found"), *cookMapBatch), false);
	}
}

bool FSpatialGDKEditorToolbarModule::CanCookMap() const
{
	// Can launch cooking only if not already running
	// Can launch cooking only if dedicated server not  already running
	// TODO: we should check for network client(s) (at least the first one)
	return !CookMapProcess.IsValid() && !ServerProcessHandle.IsValid();
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
	static const FString workingDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	static const FString serverPath = workingDir / TEXT("Binaries/Win64/CorvusServer.exe");
	const FString host = UnrealUtils::GetHostAddr();
	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(GEditor->GetEditorWorldContext().World());

	/* TODO: Server With Rendering is in fact using the Game Client with the -server option Not working with the GDK!
	https://improbableio.atlassian.net/servicedesk/customer/portal/5/GCS-1052
	if (bServerWithRendering)
	{
		static const FString clientPath = workingDir / TEXT("Binaries/Win64/Corvus.exe");
		const FString cmdArgument = FString::Printf(
			TEXT("%s -server +appName corvus +projectName corvus +workerType UnrealWorker +receptionistHost %s +useExternalIpForBridge true -log -nopauseonsuccess -NoVerifyGC -Windowed"),
			*CurrentLevelName, *host);

		ServerProcessHandle = UnrealUtils::LaunchProcess(clientPath, cmdArgument, workingDir);
	}
	*/

	// Launch the real dedicated server executable
	const FString cmdArgument = FString::Printf(
		TEXT("%s +appName corvus +projectName corvus_+deploymentName corvus_local_workflow_%s +workerType UnrealWorker +receptionistHost %s +useExternalIpForBridge true -messaging -SessionName=\"Local Workflow UnrealWorker Server\" -log -nopauseonsuccess -NoVerifyGC"),
		*CurrentLevelName, *FGenericPlatformMisc::GetDeviceId(), *host);

	ServerProcessHandle = UnrealUtils::LaunchProcess(serverPath, cmdArgument, workingDir);

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
	return !CookMapProcess.IsValid() && SpatialOSStackProcHandle.IsValid() && !ServerProcessHandle.IsValid();
}

FText FSpatialGDKEditorToolbarModule::LaunchDedicatedServerTooltip() const
{
	if (CookMapProcess.IsValid())
		return LOCTEXT("CookMapRunning_Tooltip", "Cooking map in progress.");
	else if (!SpatialOSStackProcHandle.IsValid())
		return LOCTEXT("SpatialOsNotRunning_Tooltip", "SpatialOS is not running.");
	else if (ServerProcessHandle.IsValid())
		return LOCTEXT("DedicatedServerRunning_Tooltip", "Dedicated Server is running.");
	else
		return LOCTEXT("LaunchDedicatedServer_Tooltip", "Launch a dedicated server worker.\nUses the final server executable in a new process.");
}

void FSpatialGDKEditorToolbarModule::ToggleServerWithRendering()
{
	bServerWithRendering = !bServerWithRendering;
}

bool FSpatialGDKEditorToolbarModule::IsServerWithRendering() const
{
	return bServerWithRendering;
}

void FSpatialGDKEditorToolbarModule::LaunchNetworkedClient()
{
	static const FString workingDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	static const FString clientPath = workingDir / TEXT("Binaries/Win64/Corvus.exe");
	// TODO: add parameter to launch client on a remote server
	static const FString ServerIpAddr = UnrealUtils::GetHostAddr();;

	// TODO: make -windowed argument optional
	const FString cmdArgument = FString::Printf(
		TEXT("%s +appName corvus +projectName corvus +deploymentName corvus_local_workflow_%s +workerType UnrealClient +useExternalIpForBridge true -messaging -SessionName=\"Local Workflow UnrealClient\" -log -NoVerifyGC -windowed -ResX=960 -ResY=540"),
		*ServerIpAddr, *FGenericPlatformMisc::GetDeviceId());

	FProcHandle clientProcessHandle = UnrealUtils::LaunchProcess(clientPath, cmdArgument, workingDir);

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
	return !CookMapProcess.IsValid() && SpatialOSStackProcHandle.IsValid() && ServerProcessHandle.IsValid();
}

FText FSpatialGDKEditorToolbarModule::LaunchNetworkedClientTooltip() const
{
	if (CookMapProcess.IsValid())
		return LOCTEXT("CookMapRunning_Tooltip", "Cooking map in progress.");
	else if (!SpatialOSStackProcHandle.IsValid())
		return LOCTEXT("SpatialOsNotRunning_Tooltip", "SpatialOS is not running.");
	else if (!ServerProcessHandle.IsValid())
		return LOCTEXT("DedicatedServerNotRunning_Tooltip", "Dedicated server is not running.");
	else
		return LOCTEXT("LaunchNetworkedClient_Tooltip", "Launch a networked client.\nUses the final game client executable in a new process.");
}

void FSpatialGDKEditorToolbarModule::PackageNetworkedClient()
{
	FString packageClientBatch = FPaths::RootDir() / TEXT("../Tools/LocalDemo/package_game_client.bat");
	FPaths::CollapseRelativeDirectories(packageClientBatch);
	const FString cmdArgs = FString::Printf(TEXT("/c \"%s\""), *packageClientBatch);
	if (FPaths::FileExists(packageClientBatch))
	{
		PackageClientProcess = UnrealUtils::LaunchMonitoredProcess(TEXT("Packaging Client"), TEXT("cmd.exe"), cmdArgs);
	}
	else
	{
		UnrealUtils::DisplayNotification(FString::Printf(TEXT("%s not found"), *packageClientBatch), false);
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
