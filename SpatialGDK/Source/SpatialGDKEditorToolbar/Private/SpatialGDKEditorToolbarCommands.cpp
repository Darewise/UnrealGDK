// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialGDKEditorToolbarCommands.h"

#define LOCTEXT_NAMESPACE "FSpatialGDKEditorToolbarModule"

void FSpatialGDKEditorToolbarCommands::RegisterCommands()
{
	UI_COMMAND(CreateSpatialGDKSchema, "Schema", "Creates SpatialOS Unreal GDK schema.\nYou must generate schema when you add or change any replicated properties.\nhttps://docs.improbable.io/unreal/alpha/content/schema", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(CreateSpatialGDKSchemaFull, "Schema (Full Scan)", "Creates SpatialOS Unreal GDK schema for all assets.", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(CreateSpatialGDKSnapshot, "Snapshot", "Creates SpatialOS Unreal GDK snapshot.\nYou must generate a snapshot as the starting point for your SpatialOS world when you create a new Unreal GDK project.\nhttps://docs.improbable.io/unreal/alpha/content/generating-a-snapshot", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(StartSpatialOSStackAction, "Start", "Starts a local instance of SpatialOS.", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(StopSpatialOSStackAction, "Stop", "Stops SpatialOS.", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(LaunchInspectorWebPageAction, "Inspector", "Launches default web browser to SpatialOS Inspector.", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE
