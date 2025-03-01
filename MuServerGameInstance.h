#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Networking.h"
#include "MuServerReceiveThread.h"
// 🔹 Prevenindo conflitos entre Windows e ODBC
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // Previne conflito com min/max da UE5
#endif

#include <Windows.h>  // Deve vir antes de SQL Headers
#include <sql.h>
#include <sqlext.h>

#include "MuServerGameInstance.generated.h"

// 🔹 Eventos para Blueprints
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNewConnection, FString, SocketID, FString, IP);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFinishConnection, FString, SocketID, FString, IP);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnClientPacketReceived, FString, SocketID, FString, Data);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSQLAsyncResult, FString, Label, FString, Param, int32, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnPacketReceived, FString, SocketID, uint8, HeadCode, uint8, SubCode, FString, DataString);


UCLASS()
class MUSERVER_API UMuServerGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    UMuServerGameInstance();

protected:
    virtual void Init() override;
    virtual void Shutdown() override;

public:
    UPROPERTY(BlueprintAssignable, Category = "Network")
    FOnPacketReceived OnPacketReceived;
    // 📌 Novo sistema de pacotes
    UFUNCTION(BlueprintCallable, Category = "Network")
    void SendPacket(FString SocketID, uint8 HeadCode, uint8 SubCode, const FString& DataString);

    UFUNCTION(BlueprintCallable, Category = "Network")
    static FString BytesToString(const TArray<uint8>& DataBuffer);

    UFUNCTION(BlueprintCallable, Category = "Network")
    static int32 BytesToInt(const TArray<uint8>& DataBuffer);


    UPROPERTY(BlueprintAssignable, Category = "Network")
    FOnNewConnection OnNewConnection;

    UPROPERTY(BlueprintAssignable, Category = "Network")
    FOnFinishConnection OnFinishConnection;

    UPROPERTY(BlueprintAssignable, Category = "Network")
    FOnClientPacketReceived OnClientPacketReceived;

    UPROPERTY(BlueprintAssignable, Category = "SQL")
    FOnSQLAsyncResult OnSQLAsyncResult;

    UFUNCTION(BlueprintCallable, Category = "Network")
    bool StartServer(int32 Port);

    UFUNCTION(BlueprintCallable, Category = "Network")
    void StopServer();

    UFUNCTION(BlueprintCallable, Category = "Network")
    void SendAsyncPacket(FString SocketID, FString PacketData);

    // 🔹 Métodos SQL síncronos
    UFUNCTION(BlueprintCallable, Category = "SQL")
    bool MuSQLConnect(const FString& ODBC, const FString& User, const FString& Password);

    UFUNCTION(BlueprintCallable, Category = "SQL")
    void MuSQLDisconnect();

    UFUNCTION(BlueprintCallable, Category = "SQL")
    bool MuSQLCheck();

    UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "Value1, Value2, Value3, Value4"), Category = "SQL")
    bool MuSQLExecuteQuery(const FString& Query, const FString& Value1 = TEXT(""), const FString& Value2 = TEXT(""),
        const FString& Value3 = TEXT(""), const FString& Value4 = TEXT(""));




    UFUNCTION(BlueprintCallable, Category = "SQL")
    FString MuSQLFormatQuery(const FString& QueryTemplate, const TArray<FString>& Values);

    UFUNCTION(BlueprintCallable, Category = "SQL")
    bool MuSQLFetch();

    UFUNCTION(BlueprintCallable, Category = "SQL")
    void MuSQLClose();

    UFUNCTION(BlueprintCallable, Category = "SQL")
    int32 MuSQLGetInt(const FString& ColumnName);

    UFUNCTION(BlueprintCallable, Category = "SQL")
    float MuSQLGetFloat(const FString& ColumnName);

    UFUNCTION(BlueprintCallable, Category = "SQL")
    FString MuSQLGetString(const FString& ColumnName);

    UFUNCTION(BlueprintCallable, Category = "Network")
    void DisconnectPlayer(FString SocketID);

    FCriticalSection ClientSocketsMutex;
    TMap<FString, FSocket*> ClientSockets;

    TMap<FString, double> LastPacketTime;  // Armazena o tempo do último pacote recebido
    FCriticalSection LastPacketMutex;      // 🔒 Mutex para evitar concorrência
    TMap<FString, int32> ClientErrorCount;

private:
    FSocket* ServerSocket;
    TMap<FString, FString> SQLFetchCache; // Armazena os valores da última query executada
    SQLHSTMT StatementHandle = SQL_NULL_HSTMT; // Mantém o Statement ativo

    FRunnableThread* ServerThread;
    MuServerReceiveThread* ReceiveThread;
    // 🔒 Mutex para sincronizar acessos ao mapa

    // 🔒 Conexão com ODBC
    SQLHENV EnvHandle;
    SQLHDBC ConnectionHandle;

};
