// Fill out your copyright notice in the Description page of Project Settings.


#include "MenuWidget.h"

#include "MultiplayerSessionsSubsystem.h"
#include "OnlineSubsystem.h"
#include "Components/Button.h"

void UMenuWidget::MenuSetup(int32 NumberOfPublicConnections, FString TypeOfMatch, FString LobbyPath)
{
	this->NumPublicConnections = NumberOfPublicConnections;
	this->MatchType = TypeOfMatch;
	this->PathToLobby = FString::Printf(TEXT("%s?listen"), *LobbyPath);

	this->AddToViewport(); // Attach到游戏的Viewport，全屏显示
	this->SetVisibility(ESlateVisibility::Visible); // 设置Widget的可见性
	this->bIsFocusable = true; // 为true时被点击或者被选中时会被Focus

	UWorld* World = this->GetWorld();
	if (World)
	{
		APlayerController* PlayerController = World->GetFirstPlayerController();
		if (PlayerController)
		{
			FInputModeUIOnly InputModeData;
			InputModeData.SetWidgetToFocus(this->TakeWidget()); // 获取下面的Widget，如果没有就构造它
			InputModeData.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PlayerController->SetInputMode(InputModeData);
			PlayerController->SetShowMouseCursor(true);
		}
	}

	UGameInstance* GameInstance = this->GetGameInstance();
	if (GameInstance)
	{
		MultiplayerSessionsSubsystem = GameInstance->GetSubsystem<UMultiplayerSessionsSubsystem>();
		if (MultiplayerSessionsSubsystem)
		{
			MultiplayerSessionsSubsystem->PublicOnCreateSessionCompleteDelegate.AddDynamic(
				this, &ThisClass::OnCreateSession);
			MultiplayerSessionsSubsystem->PublicOnFindSessionsCompleteDelegate.
			                              AddUObject(this, &ThisClass::OnFindSessions);
			MultiplayerSessionsSubsystem->PublicOnJoinSessionCompleteDelegate.AddUObject(
				this, &ThisClass::OnJoinSession);
			MultiplayerSessionsSubsystem->PublicOnDestroySessionCompleteDelegate.AddDynamic(
				this, &ThisClass::OnDestroySession);
			MultiplayerSessionsSubsystem->PublicOnStartSessionCompleteDelegate.
			                              AddDynamic(this, &ThisClass::OnStartSession);
		}
	}
}

bool UMenuWidget::Initialize()
{
	if (!Super::Initialize())
		return false;

	if (HostButton)
	{
		//AddDynamic是个宏，动态创建了Delegate，实际上最后还是调用了Add，
		HostButton->OnClicked.AddDynamic(this, &ThisClass::OnHostButtonClicked);
	}

	if (JoinButton)
	{
		JoinButton->OnClicked.AddDynamic(this, &ThisClass::OnJoinButtonClicked);
	}

	return true;
}

void UMenuWidget::NativeDestruct()
{
	Super::NativeDestruct();
}

void UMenuWidget::OnCreateSession(bool bWasSuccessful)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1, 15.f, FColor::Yellow,
			FString::Printf(TEXT("Session Created %s"), bWasSuccessful ? TEXT("Successful") : TEXT("Failed")));
	}

	if (bWasSuccessful)
	{
		UWorld* World = this->GetWorld();
		if (World)
		{
			World->ServerTravel(this->PathToLobby);
		}
	}
	else
	{
		HostButton->SetIsEnabled(true);
	}
}

void UMenuWidget::OnFindSessions(const TArray<FOnlineSessionSearchResult>& SessionResults, bool bWasSuccessful)
{
	if (MultiplayerSessionsSubsystem == nullptr)
	{
		return;
	}

	for (auto Result : SessionResults)
	{
		FString SessionMatchType;
		Result.Session.SessionSettings.Get(FName("MatchType"), SessionMatchType);

		if (SessionMatchType == this->MatchType)
		{
			MultiplayerSessionsSubsystem->JoinSession(Result);
			return;
		}
	}

	if (!bWasSuccessful || SessionResults.Num() == 0)
	{
		JoinButton->SetIsEnabled(true);
	}
}

void UMenuWidget::OnJoinSession(EOnJoinSessionCompleteResult::Type Result)
{
	//从Module中获取
	IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	if (OnlineSubsystem)
	{
		IOnlineSessionPtr SessionInterface = OnlineSubsystem->GetSessionInterface();
		if (SessionInterface.IsValid())
		{
			FString Address;
			//获取平台相关的连接信息，用来Travel到对应Session
			SessionInterface->GetResolvedConnectString(NAME_GameSession, Address);

			APlayerController* PlayerController = GetGameInstance()->GetFirstLocalPlayerController();
			if (PlayerController)
			{
				//Client用ClientTravel，使用完整的URL
				PlayerController->ClientTravel(Address, ETravelType::TRAVEL_Absolute);
			}
		}
	}

	if(Result != EOnJoinSessionCompleteResult::Success)
	{
		JoinButton->SetIsEnabled(true);
	}
}

void UMenuWidget::OnDestroySession(bool bWasSuccessful)
{
}

void UMenuWidget::OnStartSession(bool bWasSuccessful)
{
}

void UMenuWidget::OnHostButtonClicked()
{
	HostButton->SetIsEnabled(false);
	if (MultiplayerSessionsSubsystem)
	{
		MultiplayerSessionsSubsystem->CreateSession(NumPublicConnections, MatchType);
	}
}

void UMenuWidget::OnJoinButtonClicked()
{
	JoinButton->SetIsEnabled(false);
	if (MultiplayerSessionsSubsystem)
	{
		MultiplayerSessionsSubsystem->FindSessions(10000);
	}
}

void UMenuWidget::MenuTearDown()
{
	RemoveFromParent();
	UWorld* World = GetWorld();
	if (World)
	{
		APlayerController* PlayerController = World->GetFirstPlayerController();
		if (PlayerController)
		{
			// Focus在游戏上
			FInputModeGameOnly InputModeData;
			PlayerController->SetInputMode(InputModeData);
			PlayerController->SetShowMouseCursor(false);
		}
	}
}
