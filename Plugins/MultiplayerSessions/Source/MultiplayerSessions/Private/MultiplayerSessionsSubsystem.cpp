// Fill out your copyright notice in the Description page of Project Settings.


#include "MultiplayerSessionsSubsystem.h"

#include "OnlineSubsystem.h"

UMultiplayerSessionsSubsystem::UMultiplayerSessionsSubsystem()
	: OnCreateSessionCompleteDelegate(
		  FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnCreateSessionComplete)),
	  OnFindSessionsCompleteDelegate(
		  FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::OnFindSessionsComplete)),
	  OnJoinSessionCompleteDelegate(
		  FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnJoinSessionComplete)),
	  OnDestroySessionCompleteDelegate(
		  FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::OnDestroySessionComplete)),
	  OnStartSessionCompleteDelegate(
		  FOnStartSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnStartSessionComplete))
{
	//从Module中获取
	this->OnlineSubsystem = IOnlineSubsystem::Get();
}

bool UMultiplayerSessionsSubsystem::TryGetOnlineSessionInterface(IOnlineSessionPtr& OutOnlineSessionInterface) const
{
	if (this->OnlineSubsystem == nullptr)
	{
		return false;
	}

	const IOnlineSessionPtr OnlineSessionInterface = this->OnlineSubsystem->GetSessionInterface();
	if (!OnlineSessionInterface.IsValid())
	{
		return false;
	}

	OutOnlineSessionInterface = OnlineSessionInterface;
	return true;
}

#pragma region 对外的接口
void UMultiplayerSessionsSubsystem::CreateSession(int32 NumPublicConnections, FString MatchType)
{
	IOnlineSessionPtr OnlineSessionInterface = nullptr;
	if(!this->TryGetOnlineSessionInterface(OnlineSessionInterface))
	{
		return;
	}

	const FNamedOnlineSession* ExistingSession = OnlineSessionInterface->GetNamedSession(NAME_GameSession);
	if (ExistingSession != nullptr)
	{
		this->bCreateSessionOnDestroy = true;
		this->LastNumPublicConnections = NumPublicConnections;
		this->LastMatchType = MatchType;
		OnlineSessionInterface->DestroySession(NAME_GameSession);
		return;
	}

	// Store the delegate in a FDelegateHandle so we can later remove it from the delegate list
	this->OnCreateSessionCompleteDelegateHandle = OnlineSessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
		this->OnCreateSessionCompleteDelegate);

	LastSessionSettings = MakeShareable(new FOnlineSessionSettings());
	LastSessionSettings->bIsLANMatch = IOnlineSubsystem::Get()->GetSubsystemName() == "NULL"; //如果没连上平台，就用LAN
	LastSessionSettings->NumPublicConnections = NumPublicConnections; //公开房间的连接数量，NumPrivateConnections则是私人房间的连接数量
	LastSessionSettings->bAllowJoinInProgress = true; //是否可以在中途加入比赛
	LastSessionSettings->bAllowJoinViaPresence = true; //是否允许通过所在地加入
	LastSessionSettings->bShouldAdvertise = true; //是否公开让其他玩家加入
	LastSessionSettings->bUsesPresence = true; //是否通过所在地寻找Session
	LastSessionSettings->BuildUniqueId = 1; //用于防止不同的构建在搜索期间看到彼此
	LastSessionSettings->bUseLobbiesIfAvailable = true; //如果平台支持Lobby则优先用Lobby相关的API
	
	//Set Key-Value，设置广播模式
	LastSessionSettings->Set(FName("MatchType"), MatchType, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
	if (!OnlineSessionInterface->CreateSession(*LocalPlayer->GetPreferredUniqueNetId(), NAME_GameSession,
	                                           *LastSessionSettings))
	{
		// 清理Delegate Handle
		OnlineSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(this->OnCreateSessionCompleteDelegateHandle);
		PublicOnCreateSessionCompleteDelegate.Broadcast(false);
	}
}

void UMultiplayerSessionsSubsystem::FindSessions(int32 MaxSearchResults)
{
	IOnlineSessionPtr OnlineSessionInterface = nullptr;
	if(!this->TryGetOnlineSessionInterface(OnlineSessionInterface))
	{
		return;
	}

	this->OnFindSessionsCompleteDelegateHandle = OnlineSessionInterface->AddOnFindSessionsCompleteDelegate_Handle(
		this->OnFindSessionsCompleteDelegate);

	LastSessionSearch = MakeShareable(new FOnlineSessionSearch());
	LastSessionSearch->MaxSearchResults = MaxSearchResults;
	LastSessionSearch->bIsLanQuery = IOnlineSubsystem::Get()->GetSubsystemName() == "NULL"; //是否是查询LAN的结果
	//只找当地的Sessions(SEARCH_PRESENCE == true的Session)
	LastSessionSearch->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);
	
	const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
	if (!OnlineSessionInterface->FindSessions(*LocalPlayer->GetPreferredUniqueNetId(), LastSessionSearch.ToSharedRef()))
	{
		// 清理Delegate Handle
		OnlineSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(this->OnFindSessionsCompleteDelegateHandle);
		// 传个空的数组
		PublicOnFindSessionsCompleteDelegate.Broadcast(TArray<FOnlineSessionSearchResult>(), false);
	}
}

void UMultiplayerSessionsSubsystem::JoinSession(const FOnlineSessionSearchResult& SessionResult)
{
	IOnlineSessionPtr OnlineSessionInterface = nullptr;
	if(!this->TryGetOnlineSessionInterface(OnlineSessionInterface))
	{
		this->PublicOnJoinSessionCompleteDelegate.Broadcast(EOnJoinSessionCompleteResult::UnknownError);
		return;
	}

	this->OnJoinSessionCompleteDelegateHandle = OnlineSessionInterface->AddOnJoinSessionCompleteDelegate_Handle(this->OnJoinSessionCompleteDelegate);

	const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
	if (!OnlineSessionInterface->JoinSession(*LocalPlayer->GetPreferredUniqueNetId(), NAME_GameSession, SessionResult))
	{
		// 清理Delegate Handle
		OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(this->OnJoinSessionCompleteDelegateHandle);
		this->PublicOnJoinSessionCompleteDelegate.Broadcast(EOnJoinSessionCompleteResult::UnknownError);
	}
}

void UMultiplayerSessionsSubsystem::DestroySession()
{
	IOnlineSessionPtr OnlineSessionInterface = nullptr;
	if(!this->TryGetOnlineSessionInterface(OnlineSessionInterface))
	{
		this->PublicOnDestroySessionCompleteDelegate.Broadcast(false);
		return;
	}

	this->OnDestroySessionCompleteDelegateHandle = OnlineSessionInterface->AddOnDestroySessionCompleteDelegate_Handle(this->OnDestroySessionCompleteDelegate);

	if(!OnlineSessionInterface->DestroySession(NAME_GameSession))
	{
		// 清理Delegate Handle
		OnlineSessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(this->OnDestroySessionCompleteDelegateHandle);
		this->PublicOnDestroySessionCompleteDelegate.Broadcast(false);
	}
}

void UMultiplayerSessionsSubsystem::StartSession()
{
	IOnlineSessionPtr OnlineSessionInterface = nullptr;
	if(!this->TryGetOnlineSessionInterface(OnlineSessionInterface))
	{
		this->PublicOnStartSessionCompleteDelegate.Broadcast(false);
		return;
	}

	this->OnStartSessionCompleteDelegateHandle = OnlineSessionInterface->AddOnStartSessionCompleteDelegate_Handle(this->OnStartSessionCompleteDelegate);
	if(!OnlineSessionInterface->StartSession(NAME_GameSession))
	{
		// 清理Delegate Handle
		OnlineSessionInterface->ClearOnStartSessionCompleteDelegate_Handle(this->OnStartSessionCompleteDelegateHandle);
		this->PublicOnStartSessionCompleteDelegate.Broadcast(false);
	}
}
#pragma endregion 

#pragma region OnlineSubsystem的Session的回调
void UMultiplayerSessionsSubsystem::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	IOnlineSessionPtr OnlineSessionInterface = nullptr;
	if(this->TryGetOnlineSessionInterface(OnlineSessionInterface))
	{
		// 清理Delegate Handle
		OnlineSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(this->OnCreateSessionCompleteDelegateHandle);
	}

	PublicOnCreateSessionCompleteDelegate.Broadcast(bWasSuccessful);
}

void UMultiplayerSessionsSubsystem::OnFindSessionsComplete(bool bWasSuccessful)
{
	IOnlineSessionPtr OnlineSessionInterface = nullptr;
	if(this->TryGetOnlineSessionInterface(OnlineSessionInterface))
	{
		// 清理Delegate Handle
		OnlineSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(this->OnFindSessionsCompleteDelegateHandle);
	}

	if(LastSessionSearch->SearchResults.Num() <= 0)
	{
		PublicOnFindSessionsCompleteDelegate.Broadcast(TArray<FOnlineSessionSearchResult>(), false);
	}
	else
	{
		PublicOnFindSessionsCompleteDelegate.Broadcast(LastSessionSearch->SearchResults, bWasSuccessful);
	}
}

void UMultiplayerSessionsSubsystem::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSessionPtr OnlineSessionInterface = nullptr;
	if(this->TryGetOnlineSessionInterface(OnlineSessionInterface))
	{
		// 清理Delegate Handle
		OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(this->OnJoinSessionCompleteDelegateHandle);
	}

	PublicOnJoinSessionCompleteDelegate.Broadcast(Result);
}

void UMultiplayerSessionsSubsystem::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	IOnlineSessionPtr OnlineSessionInterface = nullptr;
	if(this->TryGetOnlineSessionInterface(OnlineSessionInterface))
	{
		// 清理Delegate Handle
		OnlineSessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(this->OnDestroySessionCompleteDelegateHandle);
	}

	if(bWasSuccessful && this->bCreateSessionOnDestroy)
	{
		this->bCreateSessionOnDestroy = false;
		CreateSession(LastNumPublicConnections, LastMatchType);
	}

	PublicOnDestroySessionCompleteDelegate.Broadcast(bWasSuccessful);
}

void UMultiplayerSessionsSubsystem::OnStartSessionComplete(FName SessionName, bool bWasSuccessful)
{
	IOnlineSessionPtr OnlineSessionInterface = nullptr;
	if(this->TryGetOnlineSessionInterface(OnlineSessionInterface))
	{
		// 清理Delegate Handle
		OnlineSessionInterface->ClearOnStartSessionCompleteDelegate_Handle(this->OnStartSessionCompleteDelegateHandle);
	}

	PublicOnStartSessionCompleteDelegate.Broadcast(bWasSuccessful);
}
#pragma endregion 
