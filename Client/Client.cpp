/*!*****************************************************************************
\file   Client.cpp
\author Rachael Chia (c.weixuanrachael@digipen.edu)
\author Ban Kai Wei Benjamin (k.ban@digipen.edu)
\date   03/12/2026
\par Course: CSD2161
\par Assignment: Assignment 4
\brief  TCP/UDP File Download Client
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*******************************************************************************/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Windows.h"
#include "ws2tcpip.h"
#include <mutex>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <unordered_map>

#pragma comment(lib, "ws2_32.lib")

// Hexa Command ID
enum CMDID {
    UNKNOWN = (unsigned char)0x0,
    REQ_QUIT = (unsigned char)0x1,
    REQ_LOGIN = (unsigned char)0x2,
    RSP_LOGIN = (unsigned char)0x3,

    REQ_BUY = (unsigned char)0x4,
    RSP_BUY = (unsigned char)0x5,

    REQ_SELL = (unsigned char)0x6,
    RSP_SELL = (unsigned char)0x7,

    REQ_PORTFOLIO = (unsigned char)0x8,
    RSP_PORTFOLIO = (unsigned char)0x9,

    REQ_HISTORY = (unsigned char)0x10,
    RSP_HISTORY = (unsigned char)0x11,

    RSP_MARKET = (unsigned char)0x12,
    RSP_MARKETUPDATE = (unsigned char)0x13,

    REQ_MARKET = (unsigned char)0x14,
    CMD_TEST = (unsigned char)0x20,
    RSP_ERROR = (unsigned char)0x25,
    ECHO_ERROR = (unsigned char)0x30
};

// Global var
volatile bool g_running = true;
std::mutex    g_outputMutex;


int recvAll(SOCKET sock, char* buffer, int length)
{
    int total = 0;
    while (total < length)
    {
        int n = recv(sock, buffer + total, length - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

bool isAllWhitespace(const std::string& s)
{
    return s.find_first_not_of(" \t\n\r\f\v") == std::string::npos;
}

std::string trim(const std::string& s)
{
    size_t first = s.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\n\r");
    return s.substr(first, last - first + 1);
}

// TCP Receiver Thread
void receiverThread(SOCKET tcpSock)
{
    while (g_running)
    {
        unsigned char cmdID;
        if (recv(tcpSock, (char*)&cmdID, 1, 0) <= 0)
        {
            g_running = false;
            break;
        }

        if (cmdID == RSP_LOGIN)
        {
            int NetBalance;
            if (recvAll(tcpSock, (char*)&NetBalance, 4) <= 0)
            {
                break;
            }
            int balance = ntohl(NetBalance);
            
            std::lock_guard<std::mutex> lk(g_outputMutex);
            std::cout << "Login successful!\n";
            std::cout << "Account Balance: $" << balance << std::endl;
        }
        
        else if (cmdID == RSP_MARKET || cmdID == RSP_MARKETUPDATE)
        {
            // Read stock count
            uint32_t stockCountNet;
            if (recvAll(tcpSock, (char*)&stockCountNet, 4) <= 0) break;
            uint32_t stockCount = ntohl(stockCountNet);

            std::lock_guard<std::mutex> lk(g_outputMutex);
            std::cout << "\n===== MARKET DATA =====\n";
            std::cout << "Symbol   | Available |    Bid  |    Ask\n";
            std::cout << "---------+-----------+---------+--------\n";

            for (uint32_t i = 0; i < stockCount; ++i)
            {
                char sym[8] = {};
                if (recvAll(tcpSock, sym, 8) <= 0) break;

                uint32_t qtyNet, bidNet, askNet;
                if (recvAll(tcpSock, (char*)&qtyNet, 4) <= 0) break;
                if (recvAll(tcpSock, (char*)&bidNet, 4) <= 0) break;
                if (recvAll(tcpSock, (char*)&askNet, 4) <= 0) break;

                std::string symbol(sym, strnlen(sym, 8));
                int qty = (int)ntohl(qtyNet);
                int bid = (int)ntohl(bidNet);
                int ask = (int)ntohl(askNet);

                // Pad symbol to 8 chars for alignment
                std::string padded = symbol;
                while (padded.size() < 8) padded += ' ';
                std::cout << padded << " | " << std::setw(9) << qty
                          << " | $" << std::setw(6) << bid
                          << " | $" << ask << "\n";
            }
            std::cout << "=======================\n";
        }

        else if (cmdID == RSP_BUY)
        {
            char sym[8] = {};
            if (recvAll(tcpSock, sym, 8) <= 0) break;
            uint32_t qtyNet, priceNet, cashNet;
            if (recvAll(tcpSock, (char*)&qtyNet, 4) <= 0) break;
            if (recvAll(tcpSock, (char*)&priceNet, 4) <= 0) break;
            if (recvAll(tcpSock, (char*)&cashNet, 4) <= 0) break;

            std::string symbol(sym, strnlen(sym, 8));
            int qty = (int)ntohl(qtyNet);
            int price = (int)ntohl(priceNet);
            int cash = (int)ntohl(cashNet);

            std::lock_guard<std::mutex> lk(g_outputMutex);
            std::cout << "\nBUY SUCCESS: " << qty << " x " << symbol
                      << " @ $" << price << " each (total: $" << qty * price << ")\n";
            std::cout << "Remaining Balance: $" << cash << "\n";
        }

        else if (cmdID == RSP_SELL)
        {
            char sym[8] = {};
            if (recvAll(tcpSock, sym, 8) <= 0) break;
            uint32_t qtyNet, priceNet, cashNet;
            if (recvAll(tcpSock, (char*)&qtyNet, 4) <= 0) break;
            if (recvAll(tcpSock, (char*)&priceNet, 4) <= 0) break;
            if (recvAll(tcpSock, (char*)&cashNet, 4) <= 0) break;

            std::string symbol(sym, strnlen(sym, 8));
            int qty = (int)ntohl(qtyNet);
            int price = (int)ntohl(priceNet);
            int cash = (int)ntohl(cashNet);

            std::lock_guard<std::mutex> lk(g_outputMutex);
            std::cout << "\nSELL SUCCESS: " << qty << " x " << symbol
                      << " @ $" << price << " each (total: $" << qty * price << ")\n";
            std::cout << "New Balance: $" << cash << "\n";
        }

        else if (cmdID == RSP_PORTFOLIO)
        {
            uint32_t cashNet, countNet;
            if (recvAll(tcpSock, (char*)&cashNet, 4) <= 0) break;
            if (recvAll(tcpSock, (char*)&countNet, 4) <= 0) break;
            int cash = (int)ntohl(cashNet);
            uint32_t count = ntohl(countNet);

            std::lock_guard<std::mutex> lk(g_outputMutex);
            std::cout << "\n===== YOUR PORTFOLIO =====\n";
            std::cout << "Cash: $" << cash << "\n";

            if (count > 0)
            {
                std::cout << "Symbol   | Quantity | Avg Cost\n";
                std::cout << "---------+----------+---------\n";
                for (uint32_t i = 0; i < count; ++i)
                {
                    char sym[8] = {};
                    if (recvAll(tcpSock, sym, 8) <= 0) break;
                    uint32_t qtyNet, costNet;
                    if (recvAll(tcpSock, (char*)&qtyNet, 4) <= 0) break;
                    if (recvAll(tcpSock, (char*)&costNet, 4) <= 0) break;

                    std::string symbol(sym, strnlen(sym, 8));
                    int qty = (int)ntohl(qtyNet);
                    int cost = (int)ntohl(costNet);

                    std::string padded = symbol;
                    while (padded.size() < 8) padded += ' ';
                    std::cout << padded << " | " << std::setw(8) << qty << " | $" << cost << "\n";
                }
            }
            else
            {
                std::cout << "(No stocks held)\n";
            }
            std::cout << "==========================\n";
        }

        else if (cmdID == RSP_HISTORY)
        {
            uint32_t msgLenNet;
            if (recvAll(tcpSock, (char*)&msgLenNet, 4) <= 0) break;
            uint32_t msgLen = ntohl(msgLenNet);

            std::string history(msgLen, '\0');
            if (msgLen > 0)
            {
                if (recvAll(tcpSock, &history[0], (int)msgLen) <= 0) break;
            }

            std::lock_guard<std::mutex> lk(g_outputMutex);
            std::cout << "\n===== TRADE HISTORY =====\n";
            if (history.empty())
                std::cout << "(No trades yet)\n";
            else
                std::cout << history;
            std::cout << "=========================\n";
        }
        
        // not enough cash, stock amount, wrong stock name/no exist
        else if (cmdID == RSP_ERROR)
        {
            uint32_t msgLen;
            if (recvAll(tcpSock, (char*)&msgLen, 4) <= 0)
            {
                break;
            }
            msgLen = ntohl(msgLen);
            std::string msg(msgLen, '\0');
            if (recvAll(tcpSock, (char*)msg.data(), msgLen) <= 0)
            {
                break;
            }
            std::lock_guard<std::mutex> lk(g_outputMutex);
            std::cout << "[ERROR] " << msg << "\n";
        }
    }
}

// Send EXACTLY n bytes
bool sendExact(SOCKET sock, const char* buffer, int length) {
    int totalSent = 0;

    while (totalSent < length) {
        int result = send(sock, buffer + totalSent, length - totalSent, 0);

        if (result == SOCKET_ERROR)
        {
            return false;
        }

        totalSent += result;
    }

    return true;
}

int main()
{
    // 1. Init Winsock
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != NO_ERROR)
    {
        std::cerr << "WSAStartup() failed.\n";
        return 1;
    }

    // 2. Get connection info from user
    std::string serverIP, serverPort, Username;
    std::cout << "Server IP Address      : "; std::getline(std::cin, serverIP);
    std::cout << "Server TCP Port Number : "; std::getline(std::cin, serverPort);

    // 3. Create TCP socket and connect to server
    SOCKET tcpSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in srvAddr{};
    srvAddr.sin_family = AF_INET;
    srvAddr.sin_port = htons((u_short)std::stoi(serverPort));
    inet_pton(AF_INET, serverIP.c_str(), &srvAddr.sin_addr);

    if (connect(tcpSock, (sockaddr*)&srvAddr, sizeof(srvAddr)) == SOCKET_ERROR)
    {
        std::cerr << "connect() failed: " << WSAGetLastError() << "\n";
        closesocket(tcpSock);
        WSACleanup();
        return 1;
    }

    std::cout << "Connected to the server!\n";

    // 4. prompt username and send REQ_LOGIN
    std::cout << "Username: ";
    std::getline(std::cin, Username);
    unsigned char loginCmd = REQ_LOGIN;
    uint32_t nameLen = htonl((uint32_t)Username.size());
    sendExact(tcpSock, (char*)&loginCmd, 1);
    sendExact(tcpSock, (char*)&nameLen, 4);
    sendExact(tcpSock, Username.c_str(), (int)Username.size());

    // 5. Start TCP receiver thread
    std::thread receiver(receiverThread, tcpSock);

    // Give receiver thread time to print login response and market data
    Sleep(500);

    std::cout << "\nCommands:\n"
              << "  /b  - Buy stocks\n"
              << "  /s  - Sell stocks\n"
              << "  /p  - View portfolio\n"
              << "  /h  - View trade history\n"
              << "  /m  - View market data\n"
              << "  /q  - Quit\n\n";

    // 7. Main loop
    std::string input;
    while (g_running && std::getline(std::cin, input))
    {
        input = trim(input);
        if (input.empty() || isAllWhitespace(input))
        {
            continue;
        }

        if (input == "/b")
        {
            std::string symbol;
            int quantity;
            {
                std::lock_guard<std::mutex> lk(g_outputMutex);
                std::cout << "Stock symbol: ";
            }
            std::getline(std::cin, symbol);
            symbol = trim(symbol);
            if (symbol.empty() || symbol.size() > 8)
            {
                std::lock_guard<std::mutex> lk(g_outputMutex);
                std::cout << "Invalid symbol (1-8 chars).\n";
                continue;
            }
            {
                std::lock_guard<std::mutex> lk(g_outputMutex);
                std::cout << "Quantity: ";
            }
            std::string qtyStr;
            std::getline(std::cin, qtyStr);
            try { quantity = std::stoi(trim(qtyStr)); }
            catch (...) {
                std::lock_guard<std::mutex> lk(g_outputMutex);
                std::cout << "Invalid quantity.\n";
                continue;
            }

            // Send REQ_BUY: cmdID(1) + symbol(8) + quantity(4)
            unsigned char cmd = REQ_BUY;
            sendExact(tcpSock, (char*)&cmd, 1);

            char symBuf[8] = {};
            memcpy(symBuf, symbol.c_str(), (symbol.size() < 8 ? symbol.size() : 8));
            sendExact(tcpSock, symBuf, 8);

            uint32_t qtyNet = htonl((uint32_t)quantity);
            sendExact(tcpSock, (char*)&qtyNet, 4);
        }

        else if (input == "/s")
        {
            std::string symbol;
            int quantity;
            {
                std::lock_guard<std::mutex> lk(g_outputMutex);
                std::cout << "Stock symbol: ";
            }
            std::getline(std::cin, symbol);
            symbol = trim(symbol);
            if (symbol.empty() || symbol.size() > 8)
            {
                std::lock_guard<std::mutex> lk(g_outputMutex);
                std::cout << "Invalid symbol (1-8 chars).\n";
                continue;
            }
            {
                std::lock_guard<std::mutex> lk(g_outputMutex);
                std::cout << "Quantity: ";
            }
            std::string qtyStr;
            std::getline(std::cin, qtyStr);
            try { quantity = std::stoi(trim(qtyStr)); }
            catch (...) {
                std::lock_guard<std::mutex> lk(g_outputMutex);
                std::cout << "Invalid quantity.\n";
                continue;
            }

            // Send REQ_SELL: cmdID(1) + symbol(8) + quantity(4)
            unsigned char cmd = REQ_SELL;
            sendExact(tcpSock, (char*)&cmd, 1);

            char symBuf[8] = {};
            memcpy(symBuf, symbol.c_str(), (symbol.size() < 8 ? symbol.size() : 8));
            sendExact(tcpSock, symBuf, 8);

            uint32_t qtyNet = htonl((uint32_t)quantity);
            sendExact(tcpSock, (char*)&qtyNet, 4);
        }

        else if (input == "/p")
        {
            unsigned char cmd = REQ_PORTFOLIO;
            sendExact(tcpSock, (char*)&cmd, 1);
        }

        else if (input == "/h")
        {
            unsigned char cmd = REQ_HISTORY;
            sendExact(tcpSock, (char*)&cmd, 1);
        }

        else if (input == "/m")
        {
            unsigned char cmd = REQ_MARKET;
            sendExact(tcpSock, (char*)&cmd, 1);
        }

        else if (input == "/q")
        {
            unsigned char cmd = REQ_QUIT;
            send(tcpSock, (char*)&cmd, 1, 0);
            g_running = false;
            break;
        }

        else
        {
            std::lock_guard<std::mutex> lk(g_outputMutex);
            std::cout << "Unknown command. Use: /b, /s, /p, /h, /m, /q\n";
        }
    }

    // 8. Clean up
    g_running = false;
    shutdown(tcpSock, SD_BOTH);
    if (receiver.joinable()) receiver.join();
    closesocket(tcpSock);
    WSACleanup();
    return 0;
}
