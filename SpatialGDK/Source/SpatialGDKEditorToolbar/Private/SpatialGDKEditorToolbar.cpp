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
#include "Misc/MessageDialog.h"
#include "SpatialGDKEditorToolbarCommands.h"
#include "SpatialGDKEditorToolbarStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "SpatialConstants.h"
#include "SpatialGDKDefaultLaunchConfigGenerator.h"
#include "SpatialGDKDefaultWorkerJsonGenerator.h"
#include "SpatialGDKEditor.h"
#include "SpatialGDKEditorSchemaGenerator.h"
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
#include "CoreGlobals.h"
#include "Misc/MonitoredProcess.h"
#include "DWCommon/Unreal/UnrealUtils.h"
#include "DWUnrealEditor/UnrealEditorUtils.h"
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

	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();

	OnPropertyChangedDelegateHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FSpatialGDKEditorToolbarModule::OnPropertyChanged);
	bStopSpatialOnExit = SpatialGDKEditorSettings->bStopSpatialOnExit;

	FSpatialGDKServicesModule& GDKServices = FModuleManager::GetModuleChecked<FSpatialGDKServicesModule>("SpatialGDKServices");
	LocalDeploymentManager = GDKServices.GetLocalDeploymentManager();
	LocalDeploymentManager->PreInit(GetDefault<USpatialGDKSettings>()->IsRunningInChina());

	LocalDeploymentManager->SetAutoDeploy(SpatialGDKEditorSettings->bAutoStartLocalDeployment);

	// Bind the play button delegate to starting a local spatial deployment.
	if (!UEditorEngine::TryStartSpatialDeployment.IsBound() && SpatialGDKEditorSettings->bAutoStartLocalDeployment)
	{
		UEditorEngine::TryStartSpatialDeployment.BindLambda([this]
		{
			VerifyAndStartDeployment();
		});
	}

	LocalDeploymentManager->Init(GetOptionalExposedRuntimeIP());
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
	UnrealUtils::UpdateProcessHandle(AIServerProcessHandle);
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

bool FSpatialGDKEditorToolbarModule::GenericSpatialOSIsVisible() const
{
	return GetDefault<UGeneralProjectSettings>()->bSpatialNetworking;
}

bool FSpatialGDKEditorToolbarModule::CanExecuteSnapshotGenerator() const
{
	return SpatialGDKEditorInstance.IsValid() && !SpatialGDKEditorInstance.Get()->IsSchemaGeneratorRunning();
}

bool FSpatialGDKEditorToolbarModule::CreateSnapshotIsVisible() const
{
	return GetDefault<UGeneralProjectSettings>()->bSpatialNetworking && GetDefault<USpatialGDKEditorSettings>()->bShowCreateSpatialSnapshot;
}

void FSpatialGDKEditorToolbarModule::MapActions(TSharedPtr<class FUICommandList> InPluginCommands)
{
	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSchema,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::SchemaGenerateButtonClicked),
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanExecuteSchemaGenerator),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::GenericSpatialOSIsVisible));
	
	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSchemaFull,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::SchemaGenerateFullButtonClicked),
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanExecuteSchemaGenerator),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::GenericSpatialOSIsVisible));

	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().DeleteSchemaDatabase,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::DeleteSchemaDatabaseButtonClicked));

	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSnapshot,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CreateSnapshotButtonClicked),
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanExecuteSnapshotGenerator),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CreateSnapshotIsVisible));

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
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LaunchInspectorCanExecute),
		FCanExecuteAction(),
		FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::GenericSpatialOSIsVisible));

	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().OpenSimulatedPlayerConfigurationWindowAction,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::ShowSimulatedPlayerDeploymentDialog),
		FCanExecuteAction(),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::ShowDeploymentDialogIsVisible));

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
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSnapshot);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().StartSpatialDeployment);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().StopSpatialDeployment);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().LaunchInspectorWebPageAction);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().OpenSimulatedPlayerConfigurationWindowAction);
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
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().DeleteSchemaDatabase);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

// CORVUS_BEGIN
TSharedRef<SWidget> FSpatialGDKEditorToolbarModule::GenerateComboMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, PluginCommands);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("ProjectSettings", "Project Settings"));
	{
		MenuBuilder.AddWidget(
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("LocalWorkflowSettingsTip", "Configure the Local Workflow and the Default Server Map to cook."))
			.WrapTextAt(220),
			FText::GetEmpty());
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowSpatialSettings", "SpatialOS Project Settings..."),
			LOCTEXT("ShowSpatialSettingsToolTip", "Open Project Settings on SpatialOS Editor setting page"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "ProjectSettings.TabIcon"),
			FUIAction(FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::ShowSpatialSettings))
		);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowMapsSettings", "Maps Project Settings..."),
			LOCTEXT("ShowMapsSettingsToolTip", "Open Project Settings on Maps & Modes setting page"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "ProjectSettings.TabIcon"),
			FUIAction(FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::ShowMapsSettings))
		);
	}
	MenuBuilder.EndSection();
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("LocalWorkflow", "Local Workflow"));
	{
		MenuBuilder.AddWidget(
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("LocalWorkflowTip", "Launch real dedicated server and networked game client(s)\nPlease execute steps in order:"))
			.WrapTextAt(220),
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
			TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CookMapLabel)),
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
	}
	MenuBuilder.EndSection();
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("Share", "Share"));
	{
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
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("Logs", "Logs"));
	{
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
		GEditor->GetEditorWorldContext().World(), Settings->GetSpatialOSSnapshotToSave(),
		FSimpleDelegate::CreateLambda([this]() { OnShowSuccessNotification("Snapshot successfully generated!"); }),
		FSimpleDelegate::CreateLambda([this]() { OnShowFailedNotification("Snapshot generation failed!"); }),
		FSpatialGDKEditorErrorHandler::CreateLambda([](FString ErrorText) { FMessageDialog::Debugf(FText::FromString(ErrorText)); }));
}

void FSpatialGDKEditorToolbarModule::DeleteSchemaDatabaseButtonClicked()
{
	if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("DeleteSchemaDatabasePrompt", "Are you sure you want to delete the schema database?")) == EAppReturnType::Yes)
	{
		OnShowTaskStartNotification(TEXT("Deleting schema database"));
		if (SpatialGDKEditor::Schema::DeleteSchemaDatabase(SpatialConstants::SCHEMA_DATABASE_FILE_PATH))
		{
			OnShowSuccessNotification(TEXT("Schema database deleted"));
		}
		else
		{
			OnShowFailedNotification(TEXT("Failed to delete schema database"));
		}
	}
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
		if (*EnableChunkInterest == TEXT("true"))
		{
			const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("The legacy flag \"enable_chunk_interest\" is set to true in the generated launch configuration. Chunk interest is not supported and this flag needs to be set to false.\n\nDo you want to configure your launch config settings now?")));

			if (Result == EAppReturnType::Yes)
			{
				FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Editor Settings");
			}

			return false;
		}
	}

	if (!SpatialGDKRuntimeSettings->bEnableHandover && SpatialGDKEditorSettings->LaunchConfigDesc.ServerWorkers.ContainsByPredicate([](const FWorkerTypeLaunchSection& Section)
		{
			return (Section.Rows * Section.Columns) > 1;
		}))
	{
		const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("Property handover is disabled and a zoned deployment is specified.\nThis is not supported.\n\nDo you want to configure your project settings now?")));

		if (Result == EAppReturnType::Yes)
		{
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Runtime Settings");
		}

		return false;
	}

	if (SpatialGDKEditorSettings->LaunchConfigDesc.ServerWorkers.ContainsByPredicate([](const FWorkerTypeLaunchSection& Section)
		{
			return (Section.Rows * Section.Columns) < Section.NumEditorInstances;
		}))
	{
		const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("Attempting to launch too many servers for load balance configuration.\nThis is not supported.\n\nDo you want to configure your project settings now?")));

		if (Result == EAppReturnType::Yes)
		{
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Editor Settings");
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

		// If the runtime IP is to be exposed, pass it to the spatial service on startup
		const USpatialGDKEditorSettings* SpatialGDKSettings = GetDefault<USpatialGDKEditorSettings>();
		const bool bSpatialServiceStarted = LocalDeploymentManager->TryStartSpatialService(GetOptionalExposedRuntimeIP());
		if (!bSpatialServiceStarted)
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
		UE_LOG(LogSpatialGDKEditorToolbar, Log, TEXT("Attempted to start a local deployment but spatial networking is disabled."));
		return;
	}

	if (!IsSnapshotGenerated())
	{
		UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Attempted to start a local deployment but snapshot is not generated."));
		return;
	}

	if (!IsSchemaGenerated())
	{
		UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Attempted to start a local deployment but schema is not generated."));
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

		bool bRedeployRequired = false;
		if (!GenerateAllDefaultWorkerJsons(bRedeployRequired))
		{
			return;
		}
		if (bRedeployRequired)
		{
			LocalDeploymentManager->SetRedeployRequired();
		}

		LaunchConfig = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir()), TEXT("Improbable/DefaultLaunchConfig.json"));
		if (const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>())
		{
			const FSpatialLaunchConfigDescription& LaunchConfigDescription = SpatialGDKEditorSettings->LaunchConfigDesc;
			GenerateDefaultLaunchConfig(LaunchConfig, &LaunchConfigDescription);
		}
	}
	else
	{
		LaunchConfig = SpatialGDKSettings->GetSpatialOSLaunchConfig();
	}

	const FString LaunchFlags = SpatialGDKSettings->GetSpatialOSCommandLineLaunchFlags();
	const FString SnapshotName = SpatialGDKSettings->GetSpatialOSSnapshotToLoad();

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, LaunchConfig, LaunchFlags, SnapshotName]
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

		FLocalDeploymentManager::LocalDeploymentCallback CallBack = [this](bool bSuccess)
		{
			if (bSuccess)
			{
				OnShowSuccessNotification(TEXT("Local deployment started!"));
			}
			else
			{
				OnShowFailedNotification(TEXT("Local deployment failed to start"));
			}
		};

		OnShowTaskStartNotification(TEXT("Starting local deployment..."));
		LocalDeploymentManager->TryStartLocalDeployment(LaunchConfig, LaunchFlags, SnapshotName, GetOptionalExposedRuntimeIP(), CallBack);
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
	FPlatformProcess::LaunchURL(TEXT("http://localhost:31000/inspector"), TEXT(""), &WebError);
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
		return GetDefault<UGeneralProjectSettings>()->bSpatialNetworking && !LocalDeploymentManager->IsLocalDeploymentRunning();
	}
	else
	{
		return GetDefault<UGeneralProjectSettings>()->bSpatialNetworking;
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

bool FSpatialGDKEditorToolbarModule::LaunchInspectorCanExecute() const
{
	return LocalDeploymentManager->IsLocalDeploymentRunning();
}

bool FSpatialGDKEditorToolbarModule::StartSpatialServiceIsVisible() const
{
	const USpatialGDKEditorSettings* SpatialGDKSettings = GetDefault<USpatialGDKEditorSettings>();

	return GetDefault<UGeneralProjectSettings>()->bSpatialNetworking && SpatialGDKSettings->bShowSpatialServiceButton && !LocalDeploymentManager->IsSpatialServiceRunning();
}

bool FSpatialGDKEditorToolbarModule::StartSpatialServiceCanExecute() const
{
	return !LocalDeploymentManager->IsServiceStarting();
}

bool FSpatialGDKEditorToolbarModule::StopSpatialServiceIsVisible() const
{
	const USpatialGDKEditorSettings* SpatialGDKSettings = GetDefault<USpatialGDKEditorSettings>();

	return GetDefault<UGeneralProjectSettings>()->bSpatialNetworking && SpatialGDKSettings->bShowSpatialServiceButton && LocalDeploymentManager->IsSpatialServiceRunning();
}

bool FSpatialGDKEditorToolbarModule::StopSpatialServiceCanExecute() const
{
	return !LocalDeploymentManager->IsServiceStopping();
}

void FSpatialGDKEditorToolbarModule::OnPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (USpatialGDKEditorSettings* Settings = Cast<USpatialGDKEditorSettings>(ObjectBeingModified))
	{
		FName PropertyName = PropertyChangedEvent.Property != nullptr
				? PropertyChangedEvent.Property->GetFName()
				: NAME_None;
		if (PropertyName.ToString() == TEXT("bStopSpatialOnExit"))
		{
			/*
			* This updates our own local copy of bStopSpatialOnExit as Settings change.
			* We keep the copy of the variable as all the USpatialGDKEditorSettings references get
			* cleaned before all the available callbacks that IModuleInterface exposes. This means that we can't access
			* this variable through its references after the engine is closed.
			*/
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

	FSlateApplication::Get().AddWindow(SimulatedPlayerDeploymentWindowPtr.ToSharedRef());
}

bool FSpatialGDKEditorToolbarModule::ShowDeploymentDialogIsVisible() const
{
	return GetDefault<UGeneralProjectSettings>()->bSpatialNetworking && GetDefault<USpatialGDKEditorSettings>()->bShowDeploymentDialog;
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

bool FSpatialGDKEditorToolbarModule::IsSnapshotGenerated() const
{
	const USpatialGDKEditorSettings* SpatialGDKSettings = GetDefault<USpatialGDKEditorSettings>();
	return FPaths::FileExists(SpatialGDKSettings->GetSpatialOSSnapshotToLoadPath());
}

bool FSpatialGDKEditorToolbarModule::IsSchemaGenerated() const
{
	FString DescriptorPath = FSpatialGDKServicesModule::GetSpatialOSDirectory(TEXT("build/assembly/schema/schema.descriptor"));
	FString GdkFolderPath = FSpatialGDKServicesModule::GetSpatialOSDirectory(TEXT("schema/unreal/gdk"));
	return FPaths::FileExists(DescriptorPath) && FPaths::DirectoryExists(GdkFolderPath) && SpatialGDKEditor::Schema::GeneratedSchemaDatabaseExists();
}

FString FSpatialGDKEditorToolbarModule::GetOptionalExposedRuntimeIP() const
{
	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
	if (SpatialGDKEditorSettings->bExposeRuntimeIP)
	{
		return SpatialGDKEditorSettings->ExposedRuntimeIP;
	}
	else
	{
		return TEXT("");
	}
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

	const USpatialGDKEditorSettings* Settings = GetDefault<USpatialGDKEditorSettings>();

	// Add additional options free-form options, as well as "-build" to automatically compile the game & server executables (but not the Editor itself) for developers :)
	TCHAR* buildOption = Settings->bLocalWorkflowCookBuild ? TEXT(" -build -nocompileeditor") : TEXT("");
	const FString CookOptions = Settings->LocalWorkflowCookCommandLineFlags + buildOption;

	const FString AutomationTool = FPaths::Combine(FPaths::RootDir(), TEXT("Engine/Binaries/DotNET/AutomationTool.exe"));
	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(GEditor->GetEditorWorldContext().World());
	const FString ProjectFilePath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());

	const FString CommandLineArgs = FString::Printf(TEXT("BuildCookRun -project=\"%s\" -unattended -noP4 -Map=%s -clientconfig=%s -platform=Win64 -server -serverconfig=%s -serverplatform=Win64 -cook -SkipCookingEditorContent -unversioned -stage %s"),
		*ProjectFilePath, *CurrentLevelName,
		EBuildConfigurations::ToString((EBuildConfigurations::Type)Settings->LocalWorkflowClientConfiguration),
		EBuildConfigurations::ToString((EBuildConfigurations::Type)Settings->LocalWorkflowServerConfiguration),
		*CookOptions);
	TCHAR* cookNotification = Settings->bLocalWorkflowCookBuild ? TEXT("Compiling and Cooking Map") : TEXT("Cooking Map");
	CookMapProcess = UnrealUtils::LaunchMonitoredProcess(cookNotification, AutomationTool, CommandLineArgs);
}

bool FSpatialGDKEditorToolbarModule::CanCookMap() const
{
	// Can launch cooking only if not already running
	// Can launch cooking only if SpatialOS not already running (because Cook Map is somehow killing any running SpatialOS)
	// Can launch cooking only if dedicated server not already running
	// TODO: we should check for network client(s) (at least the first one)
	return !CookMapProcess.IsValid() && !LocalDeploymentManager->IsLocalDeploymentRunning() && !ServerProcessHandle.IsValid();
}

FText FSpatialGDKEditorToolbarModule::CookMapLabel() const
{
	const USpatialGDKEditorSettings* Settings = GetDefault<USpatialGDKEditorSettings>();
	return Settings->bLocalWorkflowCookBuild ? LOCTEXT("BuildCookMap", "Compile and Cook Map") : LOCTEXT("CookMap", "Cook Current Map");
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
	static const FString SavedPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	static const FString ServerPath = FPaths::Combine(SavedPath, TEXT("StagedBuilds/WindowsServer/CorvusServer.exe"));
	// Dynamic settings
	const USpatialGDKEditorSettings* Settings = GetDefault<USpatialGDKEditorSettings>();
	const FString CommandLineFlags = Settings->LocalWorkflowServerCommandLineFlags;
	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(GEditor->GetEditorWorldContext().World());

	const FString CommandLineArgs = FString::Printf(
		TEXT("%s +appName corvus +projectName corvus +deploymentName corvus_local_workflow +workerType UnrealWorker +useExternalIpForBridge true -messaging -SessionName=\"Local Workflow UnrealWorker Server\" %s"),
		*CurrentLevelName, *CommandLineFlags);

	// TODO: replace by LaunchMonitoredProcess to track and display the return code
	ServerProcessHandle = UnrealUtils::LaunchProcess(ServerPath, CommandLineArgs);

	UnrealUtils::DisplayNotification(ServerProcessHandle.IsValid()
		? FString::Printf(TEXT("Dedicated Server (%s) starting on '%s'..."), *ServerPath, *CurrentLevelName)
		: FString::Printf(TEXT("Failed to start Dedicated Server (%s)"), *ServerPath),
		ServerProcessHandle.IsValid());

	// Launch the AI worker server executable
	if (GetDefault<USpatialGDKSettings>()->bEnableOffloading)
	{
		const FString AICommandLineArgs = FString::Printf(
			TEXT("%s +appName corvus +projectName corvus +deploymentName corvus_local_workflow +workerType AIWorker +useExternalIpForBridge true -messaging -SessionName=\"Local Workflow AIWorker Server\" %s"),
			*CurrentLevelName, *CommandLineFlags);

		AIServerProcessHandle = UnrealUtils::LaunchProcess(ServerPath, AICommandLineArgs);

		UnrealUtils::DisplayNotification(AIServerProcessHandle.IsValid()
			? FString::Printf(TEXT("AI Worker (%s) starting on '%s'..."), *ServerPath, *CurrentLevelName)
			: TEXT("Failed to start AI Worker"),
			AIServerProcessHandle.IsValid());
	}
}

bool FSpatialGDKEditorToolbarModule::CanLaunchDedicatedServer() const
{
	// Can launch dedicated server only if cooking is not running
	// Can launch dedicated server only if SpatialOS is running OR is not used
	// Can launch dedicated server only if not already running
	return !CookMapProcess.IsValid() && (LocalDeploymentManager->IsLocalDeploymentRunning() || !GetDefault<UGeneralProjectSettings>()->bSpatialNetworking) && !ServerProcessHandle.IsValid();
}

FText FSpatialGDKEditorToolbarModule::LaunchDedicatedServerTooltip() const
{
	if (CookMapProcess.IsValid())
		return LOCTEXT("CookMapRunning_Tooltip", "Cooking map in progress.");
	else if (!LocalDeploymentManager->IsLocalDeploymentRunning() && GetDefault<UGeneralProjectSettings>()->bSpatialNetworking)
		return LOCTEXT("SpatialOsNotRunning_Tooltip", "SpatialOS is not running.");
	else if (ServerProcessHandle.IsValid())
		return LOCTEXT("DedicatedServerRunning_Tooltip", "Dedicated Server is running.");
	else
		return LOCTEXT("LaunchDedicatedServer_Tooltip", "Launch a dedicated server worker in a new process.\nUses the final server executable.");
}

void FSpatialGDKEditorToolbarModule::LaunchNetworkedClient()
{
	static const FString SavedPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	static const FString ClientPath = FPaths::Combine(SavedPath, TEXT("StagedBuilds/WindowsNoEditor/Corvus.exe"));
	// Dynamic settings
	const USpatialGDKEditorSettings* Settings = GetDefault<USpatialGDKEditorSettings>();
	const FString ServerIpAddr = Settings->LocalWorkflowServerIpAddr.IsEmpty() ? TEXT("127.0.0.1") : Settings->LocalWorkflowServerIpAddr;
	const FString CommandLineFlags = Settings->LocalWorkflowClientCommandLineFlags;

	const FString CommandLineArgs = FString::Printf(
		TEXT("%s -game +appName corvus +projectName corvus +deploymentName corvus_local_workflow +workerType UnrealClient +useExternalIpForBridge true -messaging -SessionName=\"Local Workflow UnrealClient\" %s"),
		*ServerIpAddr, *CommandLineFlags);

	// TODO: replace by LaunchMonitoredProcess to track and display the return code
	FProcHandle ClientProcessHandle = UnrealUtils::LaunchProcess(ClientPath, CommandLineArgs);

	UnrealUtils::DisplayNotification(ClientProcessHandle.IsValid()
		? FString::Printf(TEXT("Networked Client (%s) starting on '%s'..."), *ClientPath, *ServerIpAddr)
		: FString::Printf(TEXT("Failed to start Networked Client (%s)"), *ClientPath),
		ClientProcessHandle.IsValid());
	FPlatformProcess::CloseProc(ClientProcessHandle);
}

bool FSpatialGDKEditorToolbarModule::CanLaunchNetworkedClient() const
{
	// Can launch networked client only if cooking is not running
	// Can launch networked client only if SpatialOS is running OR is not used
	// Can launch networked client only with the dedicated server running
	return !CookMapProcess.IsValid() && (LocalDeploymentManager->IsLocalDeploymentRunning() || !GetDefault<UGeneralProjectSettings>()->bSpatialNetworking) && ServerProcessHandle.IsValid();
}

FText FSpatialGDKEditorToolbarModule::LaunchNetworkedClientTooltip() const
{
	if (CookMapProcess.IsValid())
		return LOCTEXT("CookMapRunning_Tooltip", "Cooking map in progress.");
	else if (!LocalDeploymentManager->IsLocalDeploymentRunning() && GetDefault<UGeneralProjectSettings>()->bSpatialNetworking)
		return LOCTEXT("SpatialOsNotRunning_Tooltip", "SpatialOS is not running.");
	else if (!ServerProcessHandle.IsValid())
		return LOCTEXT("DedicatedServerNotRunning_Tooltip", "Dedicated server is not running.");
	else
		return LOCTEXT("LaunchNetworkedClient_Tooltip", "Launch a networked client in a new process.\nUses the final game client executable.");
}

void FSpatialGDKEditorToolbarModule::ExploreDedicatedServerLogs() const
{
	static const FString SavedPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	static const FString LogsPath = FPaths::Combine(SavedPath, TEXT("StagedBuilds/WindowsServer/Corvus/Saved/Logs/"));
	FPlatformProcess::ExploreFolder(*LogsPath);
}

void FSpatialGDKEditorToolbarModule::ExploreNetworkedClientLogs()
{
	static const FString SavedPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	static const FString LogsPath = FPaths::Combine(SavedPath, TEXT("StagedBuilds/WindowsNoEditor/Corvus/Saved/Logs/"));
	FPlatformProcess::ExploreFolder(*LogsPath);
}

void FSpatialGDKEditorToolbarModule::ShowSpatialSettings()
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Editor Settings");
}

void FSpatialGDKEditorToolbarModule::ShowMapsSettings()
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Project", "Maps");
}

void FSpatialGDKEditorToolbarModule::PackageNetworkedClient()
{
	FString PackageClientBatch = FPaths::Combine(FPaths::RootDir(), TEXT("../Tools/LocalDemo/package_game_client.bat"));
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
	// Compare current & default map from settings to control the package of the network client: what matters is what the server loads
	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(GEditor->GetEditorWorldContext().World());
	const FString ServerDefaultMap = UnrealUtils::GetServerDefaultMap();

	// Can package networked client only if cooking is not running
	// Can package networked client only if packaging not already in progress
	// Can package networked client only if configured Server Default Map matches Current Level
	return !CookMapProcess.IsValid() && !PackageClientProcess.IsValid() && ServerDefaultMap.EndsWith(CurrentLevelName);
}

FText FSpatialGDKEditorToolbarModule::PackageNetworkedClientTooltip() const
{
	// Compare current & default map from settings to control the package of the network client: what matters is what the server loads
	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(GEditor->GetEditorWorldContext().World());
	const FString ServerDefaultMap = UnrealUtils::GetServerDefaultMap();

	if (CookMapProcess.IsValid())
		return LOCTEXT("CookMapRunning_Tooltip", "Cooking map in progress.");
	else if (PackageClientProcess.IsValid())
		return LOCTEXT("PackageNetworkedClientRunning_Tooltip", "Packaging client in progress.");
	else if (!ServerDefaultMap.EndsWith(CurrentLevelName))
		return LOCTEXT("WrongDefaultMap_Tooltip", "Server Default Map does not match current map. Set it properly in Project Settings.");
	else
		return LOCTEXT("PackageNetworkedClient_Tooltip", "Package a networked client with Server Default Map for remote multi-player game.\nWill connect to the dedicated server at the current IP Address.");
}

// CORVUS_END

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSpatialGDKEditorToolbarModule, SpatialGDKEditorToolbar)
