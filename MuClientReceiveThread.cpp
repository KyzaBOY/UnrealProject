#include "MuClientReceiveThread.h"
#include "MuClientGameInstance.h"
#include "Async/Async.h"

// Construtor
MuClientReceiveThread::MuClientReceiveThread(FSocket* InSocket, UMuClientGameInstance* InClient)
{
    ClientSocket = InSocket;
    OwnerClient = InClient;
    bRunning = true;
}

// Destrutor
MuClientReceiveThread::~MuClientReceiveThread()
{
    bRunning = false;
}

uint32 MuClientReceiveThread::Run()
{
    while (bRunning && ClientSocket && ClientSocket->GetConnectionState() == ESocketConnectionState::SCS_Connected)
    {
        uint16 PacketSize = 0;
        int32 BytesRead = 0;

        // Ler os primeiros 2 bytes (tamanho do pacote)
        if (!ClientSocket->Recv((uint8*)&PacketSize, sizeof(uint16), BytesRead) || BytesRead != sizeof(uint16))
        {
            continue; // Se falhar, ignoramos
        }

        // Criar buffer do tamanho correto
        TArray<uint8> PacketBuffer;
        PacketBuffer.SetNumUninitialized(PacketSize + 1); // Adiciona espaço para um terminador nulo

        // Ler os dados do pacote
        int32 TotalBytesRead = 0;
        while (TotalBytesRead < PacketSize)
        {
            int32 BytesReadNow = 0;
            if (!ClientSocket->Recv(PacketBuffer.GetData() + TotalBytesRead, PacketSize - TotalBytesRead, BytesReadNow) || BytesReadNow <= 0)
            {
                break; // Se falhar, saímos do loop
            }
            TotalBytesRead += BytesReadNow;
        }

        // Se não lemos o pacote inteiro, ignoramos
        if (TotalBytesRead != PacketSize)
        {
            continue;
        }

        // Adicionar terminador nulo para evitar lixo
        PacketBuffer[PacketSize] = '\0';

        // Converter para FString corretamente
        FString ReceivedData = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(PacketBuffer.GetData())));

        // Remover espaços ou caracteres extras
        ReceivedData.TrimStartAndEndInline();

        UE_LOG(LogTemp, Log, TEXT("📩 Pacote recebido: %s"), *ReceivedData);

        // 🔹 Processar o pacote na Game Thread
        if (OwnerClient)
        {
            AsyncTask(ENamedThreads::GameThread, [this, ReceivedData]()
                {
                    OwnerClient->OnPacketReceived.Broadcast(ReceivedData);
                });
        }
    }

    return 0;
}
