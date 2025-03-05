#include "MuServerGameInstance.h"

#include "Async/Async.h"
UMuServerGameInstance::UMuServerGameInstance()
{
    ServerSocket = nullptr;
    ReceiveThread = nullptr;
    ServerThread = nullptr;

    EnvHandle = SQL_NULL_HENV;
    ConnectionHandle = SQL_NULL_HDBC;
    StatementHandle = SQL_NULL_HSTMT;
}

void UMuServerGameInstance::Init()
{
    Super::Init();
}

void UMuServerGameInstance::Shutdown()
{
    MuSQLDisconnect();
    StopServer();
    Super::Shutdown();
}

bool UMuServerGameInstance::StartServer(int32 Port)
{
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

    ServerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("MuServerSocket"), false);
    if (!ServerSocket)
    {
        UE_LOG(LogTemp, Error, TEXT("Falha ao criar o socket do servidor."));
        return false;
    }

    // Criando o endereço do servidor
    TSharedPtr<FInternetAddr> ServerAddress = SocketSubsystem->CreateInternetAddr();

    // 🔹 Definindo o IP específico (substitua pelo seu IP real)
    bool bIsValid;
    ServerAddress->SetIp(TEXT("26.195.133.181"), bIsValid);

    if (!bIsValid)
    {
        UE_LOG(LogTemp, Error, TEXT("Endereço IP inválido."));
        return false;
    }

    ServerAddress->SetPort(Port);

    if (!ServerSocket->Bind(*ServerAddress))
    {
        UE_LOG(LogTemp, Error, TEXT("Falha ao vincular o servidor ao IP %s e porta %d."), *ServerAddress->ToString(true), Port);
        return false;
    }

    if (!ServerSocket->Listen(8))  // Permitir até 8 conexões simultâneas
    {
        UE_LOG(LogTemp, Error, TEXT("Falha ao colocar o servidor em modo de escuta."));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("✅ Servidor iniciado no IP %s na porta %d!"), *ServerAddress->ToString(true), Port);

    ReceiveThread = new MuServerReceiveThread(ServerSocket, this);
    ServerThread = FRunnableThread::Create(ReceiveThread, TEXT("MuServerReceiveThread"));

    return true;
}


void UMuServerGameInstance::StopServer()
{
    UE_LOG(LogTemp, Log, TEXT("Finalizando o servidor..."));

    // 🚨 Primeiro, pare a thread de recebimento
    if (ReceiveThread)
    {
        ReceiveThread->Stop();
        ServerThread->WaitForCompletion(); // Aguarde a thread finalizar
        delete ServerThread;
        ServerThread = nullptr;

        delete ReceiveThread;
        ReceiveThread = nullptr;
    }

    // 🚨 Agora feche os sockets APÓS garantir que a thread parou
    if (ServerSocket)
    {
        ServerSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ServerSocket);
        ServerSocket = nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("Servidor desligado com sucesso."));
}

void UMuServerGameInstance::SendAsyncPacket(FString SocketID, FString PacketData)
{
    UE_LOG(LogTemp, Log, TEXT("📤 Tentando enviar pacote para SocketID: %s"), *SocketID);

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, SocketID, PacketData]()
        {
            FSocket* ClientSocket = nullptr;

            // 🔒 Acesso seguro ao mapa de sockets
            ClientSocketsMutex.Lock();
            if (ClientSockets.Contains(SocketID))
            {
                ClientSocket = ClientSockets[SocketID];
            }
            ClientSocketsMutex.Unlock();

            if (!ClientSocket)
            {
                UE_LOG(LogTemp, Error, TEXT("❌ ClientSocket é NULL para SocketID: %s"), *SocketID);
                UE_LOG(LogTemp, Error, TEXT("🔎 Cliente pode ter sido removido ou nunca foi adicionado corretamente!"));
                return;
            }

            if (ClientSocket->GetConnectionState() != ESocketConnectionState::SCS_Connected)
            {
                UE_LOG(LogTemp, Warning, TEXT("⚠️ O Cliente %s não está mais conectado."), *SocketID);
                return;
            }

            // 🔹 Convertendo a string para UTF-8
            FTCHARToUTF8 Converter(*PacketData);
            int32 DataSize = Converter.Length();

            // 🔹 Criando um buffer para envio (tamanho + dados)
            TArray<uint8> Buffer;
            Buffer.SetNum(2 + DataSize);

            // 🔹 Armazena o tamanho do pacote nos primeiros 2 bytes
            uint16 PacketSize = static_cast<uint16>(DataSize);
            FMemory::Memcpy(Buffer.GetData(), &PacketSize, sizeof(uint16));

            // 🔹 Armazena os dados logo em seguida
            FMemory::Memcpy(Buffer.GetData() + 2, Converter.Get(), DataSize);

            // 🔹 Enviar o pacote completo
            int32 BytesSent = 0;
            bool bSuccess = ClientSocket->Send(Buffer.GetData(), Buffer.Num(), BytesSent);

            if (bSuccess)
            {
                UE_LOG(LogTemp, Log, TEXT("✅ Pacote enviado para %s (%d bytes): %s"), *SocketID, BytesSent, *PacketData);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("❌ Falha ao enviar pacote para %s!"), *SocketID);
            }
        });
}


void UMuServerGameInstance::DisconnectPlayer(FString SocketID)
{
    UE_LOG(LogTemp, Log, TEXT("🔴 Desconectando jogador: %s"), *SocketID);

    ClientSocketsMutex.Lock();

    if (!ClientSockets.Contains(SocketID))
    {
        UE_LOG(LogTemp, Warning, TEXT("⚠️ Tentativa de desconectar um SocketID inexistente: %s"), *SocketID);
        ClientSocketsMutex.Unlock();
        return;
    }

    FSocket* ClientSocket = ClientSockets[SocketID];

    if (ClientSocket)
    {
        ClientSocket->Close(); // Fecha a conexão de forma segura
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
    }

    ClientSockets.Remove(SocketID);
    ClientSocketsMutex.Unlock();

    // 📌 Emite o evento de desconexão para os Blueprints
    AsyncTask(ENamedThreads::GameThread, [this, SocketID]()
        {
            OnFinishConnection.Broadcast(SocketID, TEXT("Desconhecido"));
        });

    UE_LOG(LogTemp, Log, TEXT("✅ Jogador %s foi desconectado com sucesso!"), *SocketID);
}

void UMuServerGameInstance::SendPacket(FString SocketID, uint8 HeadCode, uint8 SubCode, const FString& DataString)
{
    TArray<uint8> Data;
    FTCHARToUTF8 Converter(*DataString);
    Data.Append((uint8*)Converter.Get(), Converter.Length());

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, SocketID, HeadCode, SubCode, Data]()
        {
            FSocket* ClientSocket = nullptr;

            ClientSocketsMutex.Lock();
            if (ClientSockets.Contains(SocketID))
            {
                ClientSocket = ClientSockets[SocketID];
            }
            ClientSocketsMutex.Unlock();

            if (!ClientSocket || ClientSocket->GetConnectionState() != ESocketConnectionState::SCS_Connected)
            {
                UE_LOG(LogTemp, Warning, TEXT("⚠️ Cliente não está conectado: %s"), *SocketID);
                return;
            }

            int32 PacketSize = 3 + Data.Num();
            TArray<uint8> Buffer;
            Buffer.SetNum(PacketSize);

            Buffer[0] = PacketSize;
            Buffer[1] = HeadCode;
            Buffer[2] = SubCode;

            if (Data.Num() > 0)
            {
                FMemory::Memcpy(Buffer.GetData() + 3, Data.GetData(), Data.Num());
            }

            int32 BytesSent = 0;
            bool bSuccess = ClientSocket->Send(Buffer.GetData(), Buffer.Num(), BytesSent);

            if (bSuccess)
            {
                UE_LOG(LogTemp, Log, TEXT("✅ Pacote enviado para %s (%d bytes)"), *SocketID, BytesSent);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("❌ Falha ao enviar pacote"));
            }
        });
}

FString UMuServerGameInstance::BytesToString(const TArray<uint8>& DataBuffer)
{
    return FString(ANSI_TO_TCHAR(reinterpret_cast<const char*>(DataBuffer.GetData())));
}

int32 UMuServerGameInstance::BytesToInt(const TArray<uint8>& DataBuffer)
{
    int32 Value = 0;
    if (DataBuffer.Num() >= 4) // Garantir que temos bytes suficientes
    {
        FMemory::Memcpy(&Value, DataBuffer.GetData(), sizeof(int32));
    }
    return Value;
}


bool UMuServerGameInstance::MuSQLConnect(const FString& ODBC, const FString& User, const FString& Password)
{
    SQLRETURN RetCode;

    // 🔹 Aloca ambiente ODBC
    RetCode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &EnvHandle);
    if (RetCode != SQL_SUCCESS && RetCode != SQL_SUCCESS_WITH_INFO)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Falha ao alocar ambiente ODBC."));
        return false;
    }

    // 🔹 Define a versão ODBC
    RetCode = SQLSetEnvAttr(EnvHandle, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (RetCode != SQL_SUCCESS && RetCode != SQL_SUCCESS_WITH_INFO)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Falha ao definir versão do ODBC."));
        SQLFreeHandle(SQL_HANDLE_ENV, EnvHandle);
        return false;
    }

    // 🔹 Aloca conexão
    RetCode = SQLAllocHandle(SQL_HANDLE_DBC, EnvHandle, &ConnectionHandle);
    if (RetCode != SQL_SUCCESS && RetCode != SQL_SUCCESS_WITH_INFO)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Falha ao alocar conexão ODBC."));
        SQLFreeHandle(SQL_HANDLE_ENV, EnvHandle);
        return false;
    }

    // 🔹 String de conexão (garanta que os dados estão corretos)
    FString ConnString = TEXT("DRIVER={ODBC Driver 11 for SQL Server};SERVER=(local);DATABASE=VERSUSMU;UID=sa;PWD=wz1wqgwp;");


    // 🔹 Buffer para armazenar a string de conexão
    SQLCHAR ConnStr[1024] = { 0 };
    SQLSMALLINT OutConnStrLen;

    // 🔹 Converte FString para ANSI (evita problemas com encoding)
    FCStringAnsi::Strncpy((char*)ConnStr, TCHAR_TO_UTF8(*ConnString), sizeof(ConnStr));

    // 🔹 Obtém o tamanho correto da string
    SQLSMALLINT ConnStrLen = static_cast<SQLSMALLINT>(strlen((char*)ConnStr));

    // 🔹 Log para verificar a string antes da conexão
    UE_LOG(LogTemp, Log, TEXT("🔎 Tentando conectar com string: %s"), *ConnString);

    // 🔹 Tenta conectar ao banco de dados
    RetCode = SQLDriverConnectA(
        ConnectionHandle,  // Handle de conexão válido
        NULL,              // Handle da janela (NULL para conexões sem UI)
        ConnStr,           // String de conexão convertida
        SQL_NTS,           // Tamanho correto da string
        NULL,              // Buffer de saída (NULL se não precisar)
        0,                 // Tamanho do buffer de saída (0 se NULL)
        &OutConnStrLen,    // Ponteiro para armazenar o tamanho final
        SQL_DRIVER_NOPROMPT // Tipo de conexão (sem UI)
    );

    // 🔹 Verifica sucesso da conexão
    if (RetCode == SQL_SUCCESS || RetCode == SQL_SUCCESS_WITH_INFO)
    {
        UE_LOG(LogTemp, Log, TEXT("✅ Conexão ODBC bem-sucedida!"));
        return true;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Falha ao conectar ao banco via ODBC. Código de erro: %d"), RetCode);

        // 🔹 Captura a mensagem de erro detalhada
        SQLCHAR SQLState[6], MessageText[256];
        SQLINTEGER NativeError;
        SQLSMALLINT TextLength;

        if (SQLGetDiagRecA(SQL_HANDLE_DBC, ConnectionHandle, 1, SQLState, &NativeError, MessageText, sizeof(MessageText), &TextLength) == SQL_SUCCESS)
        {
            UE_LOG(LogTemp, Error, TEXT("🔴 ODBC Error: SQLState=%s, Message=%s"), ANSI_TO_TCHAR((char*)SQLState), ANSI_TO_TCHAR((char*)MessageText));
        }

        // 🔹 Libera recursos em caso de falha
        MuSQLDisconnect();
        SQLFreeHandle(SQL_HANDLE_DBC, ConnectionHandle);
        SQLFreeHandle(SQL_HANDLE_ENV, EnvHandle);

        return false;
    }
}
// 📌 Desconectar do banco de dados
void UMuServerGameInstance::MuSQLDisconnect()
{
    if (ConnectionHandle)
    {
        SQLDisconnect(ConnectionHandle);
        SQLFreeHandle(SQL_HANDLE_DBC, ConnectionHandle);
        ConnectionHandle = nullptr;
    }

    if (EnvHandle)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, EnvHandle);
        EnvHandle = nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("Desconectado do banco de dados com sucesso."));
}

// 📌 Verificar conexão
bool UMuServerGameInstance::MuSQLCheck()
{
    return ConnectionHandle != SQL_NULL_HDBC;
}



bool UMuServerGameInstance::MuSQLExecuteQuery(const FString& Query, const FString& Value1, const FString& Value2,
    const FString& Value3, const FString& Value4)
{
    if (!MuSQLCheck())
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Não há conexão ativa com o banco de dados."));
        return false;
    }

    FString FormattedQuery = Query;

    // Substituindo %1, %2, %3, %4 pelos valores fornecidos
    FormattedQuery = FormattedQuery.Replace(TEXT("%1"), *Value1);
    FormattedQuery = FormattedQuery.Replace(TEXT("%2"), *Value2);
    FormattedQuery = FormattedQuery.Replace(TEXT("%3"), *Value3);
    FormattedQuery = FormattedQuery.Replace(TEXT("%4"), *Value4);

    SQLRETURN RetCode;
    RetCode = SQLAllocHandle(SQL_HANDLE_STMT, ConnectionHandle, &StatementHandle);
    if (RetCode != SQL_SUCCESS && RetCode != SQL_SUCCESS_WITH_INFO)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Falha ao alocar StatementHandle."));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("🔎 Executando query: %s"), *FormattedQuery);

    RetCode = SQLExecDirect(StatementHandle, (SQLTCHAR*)*FormattedQuery, SQL_NTS);
    if (RetCode != SQL_SUCCESS && RetCode != SQL_SUCCESS_WITH_INFO)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Erro ao executar a query."));
        SQLFreeHandle(SQL_HANDLE_STMT, StatementHandle);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("✅ Query executada com sucesso!"));
    return true;
}



FString UMuServerGameInstance::MuSQLFormatQuery(const FString& QueryTemplate, const TArray<FString>& Values)
{
    FString FormattedQuery = QueryTemplate;
    for (int32 i = 0; i < Values.Num(); i++)
    {
        FormattedQuery = FormattedQuery.Replace(*FString::Printf(TEXT("%%%d"), i + 1), *Values[i]);
    }
    return FormattedQuery;
}


bool UMuServerGameInstance::MuSQLFetch()
{
    if (!MuSQLCheck())
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Não há conexão ativa com o banco de dados."));
        return false;
    }

    if (StatementHandle == SQL_NULL_HSTMT)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Nenhuma query foi executada!"));
        return false;
    }

    // Limpa o cache antes de armazenar novos resultados
    SQLFetchCache.Empty();

    SQLRETURN RetCode;
    SQLSMALLINT NumCols = 0;

    // Obtém a quantidade de colunas retornadas pela última query
    RetCode = SQLNumResultCols(StatementHandle, &NumCols);
    if (RetCode != SQL_SUCCESS && RetCode != SQL_SUCCESS_WITH_INFO)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Falha ao obter número de colunas."));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("🔎 A consulta retornou %d colunas."), NumCols);

    // Armazena os valores das colunas no cache
    if (SQLFetch(StatementHandle) == SQL_SUCCESS)
    {
        for (SQLSMALLINT i = 1; i <= NumCols; i++)
        {
            SQLWCHAR ColumnName[128];  // Nome da coluna
            SQLSMALLINT ColumnNameLen;
            SQLCHAR ColumnValue[1024]; // Valor da coluna
            SQLLEN ValueLenOrInd;  // Indicador de comprimento do valor

            // Obtendo o nome da coluna
            SQLDescribeCol(StatementHandle, i, ColumnName, sizeof(ColumnName) / sizeof(SQLWCHAR), &ColumnNameLen, NULL, NULL, NULL, NULL);

            // Obtendo o valor da coluna
            RetCode = SQLGetData(StatementHandle, i, SQL_C_CHAR, ColumnValue, sizeof(ColumnValue), &ValueLenOrInd);

            // Converter SQLWCHAR para FString
            FString ColName = FString(ColumnName);
            FString ColValue;

            if (RetCode == SQL_SUCCESS || RetCode == SQL_SUCCESS_WITH_INFO)
            {
                // Se o valor NÃO for NULL, converte normalmente
                if (ValueLenOrInd != SQL_NULL_DATA)
                {
                    ColValue = FString(ANSI_TO_TCHAR((char*)ColumnValue));
                }
                else
                {
                    ColValue = TEXT("NULL");  // Retorna "NULL" quando o dado for NULL no SQL
                }
            }
            else
            {
                ColValue = TEXT("NULL");  // Retorna "NULL" caso ocorra um erro na leitura
            }

            // Armazena no cache
            SQLFetchCache.Add(ColName, ColValue);
        }

        UE_LOG(LogTemp, Log, TEXT("✅ Dados recebidos e armazenados no cache."));
        return true;
    }

    UE_LOG(LogTemp, Warning, TEXT("⚠️ Nenhum dado encontrado na última consulta."));
    return false;
}





void UMuServerGameInstance::MuSQLClose()
{
    SQLFetchCache.Empty();

    if (StatementHandle != SQL_NULL_HSTMT)
    {
        SQLFreeHandle(SQL_HANDLE_STMT, StatementHandle);
        StatementHandle = SQL_NULL_HSTMT;
    }

    UE_LOG(LogTemp, Log, TEXT("🔒 Query fechada e cache limpo."));
}

int32 UMuServerGameInstance::MuSQLGetInt(const FString& ColumnName)
{
    if (SQLFetchCache.Contains(ColumnName))
    {
        FString Value = SQLFetchCache[ColumnName];
        if (Value == TEXT("NULL")) return -1;  // Se for NULL, retorna -1
        return FCString::Atoi(*Value);
    }

    UE_LOG(LogTemp, Warning, TEXT("⚠️ Coluna '%s' não encontrada."), *ColumnName);
    return -1;  // Retorna -1 caso a coluna não exista
}



float UMuServerGameInstance::MuSQLGetFloat(const FString& ColumnName)
{
    if (SQLFetchCache.Contains(ColumnName))
    {
        FString Value = SQLFetchCache[ColumnName];

        // Se o valor for NULL, retorna um valor padrão
        if (Value == TEXT("NULL")) return -1.1f;

        // Remover qualquer separador de milhares
        Value.ReplaceInline(TEXT(","), TEXT(""));

        return FCString::Atof(*Value);
    }

    UE_LOG(LogTemp, Warning, TEXT("⚠️ Coluna '%s' não encontrada."), *ColumnName);
    return -1.1f;  // Retorna -1.1 caso a coluna não exista
}



FString UMuServerGameInstance::MuSQLGetString(const FString& ColumnName)
{
    if (SQLFetchCache.Contains(ColumnName))
    {
        return SQLFetchCache[ColumnName];
    }

    UE_LOG(LogTemp, Warning, TEXT("⚠️ Coluna '%s' não encontrada."), *ColumnName);
    return TEXT("NULL");
}
