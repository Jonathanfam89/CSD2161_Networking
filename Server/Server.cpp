/*!*****************************************************************************
\file   Server.cpp
\author Rachael Chia (c.weixuanrachael@digipen.edu)
\author Ban Kai Wei Benjamin (k.ban@digipen.edu)
\author Fam Qian Wei jonathan (fam.q@digipen.edu)
\author Zong Han Law (zonghan.law@digipen.edu)
\author Muhammad Raziq (muhammadraziq.b@digipen.edu)

\date   03/28/2026
\par Course: CSD2161
\par Assignment: Assignment 5
\brief  This file is the Server-side implementation of a TCP LAN 
		online trading platform.

        Manages connected clients, processes trading commands, updates
        portfolios and market prices, and persists user/trade data.
		
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*******************************************************************************/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX

#include "Windows.h"		// Entire Win32 API...
// #include "winsock2.h"	// ...or Winsock alone 
#include "ws2tcpip.h"		// getaddrinfo()
#pragma comment(lib, "ws2_32.lib")

#include <mutex>
#include <thread>
#include <string>			
#include <vector>		
#include <fstream>
#include <sstream>
#include <cstring>			// memcpy
#include <ctime>
#include <chrono>
#include <iostream>
#include <filesystem>
#include "taskqueue.h"
#include <unordered_map>
#include <math.h>

// Hexa Command ID
enum CMDID {
	UNKNOWN   = (unsigned char)0x0,
	REQ_QUIT  = (unsigned char)0x1,
	REQ_LOGIN = (unsigned char)0x2,
	RSP_LOGIN = (unsigned char)0x3,

	// /b
	REQ_BUY  = (unsigned char)0x4,  
	RSP_BUY  = (unsigned char)0x5,

	// /s
	REQ_SELL = (unsigned char)0x6,    
	RSP_SELL = (unsigned char)0x7,

	// /p
	REQ_PORTFOLIO = (unsigned char)0x8,
	RSP_PORTFOLIO = (unsigned char)0x9,

	// /h
	REQ_HISTORY = (unsigned char)0x10,  
	RSP_HISTORY = (unsigned char)0x11,

	// auto at start and when someone buy/sell smth
	RSP_MARKET       = (unsigned char)0x12,
	RSP_MARKETUPDATE = (unsigned char)0x13,

	REQ_MARKET = (unsigned char)0x14,
	CMD_TEST   = (unsigned char)0x20,
	RSP_ERROR  = (unsigned char)0x25,
	ECHO_ERROR = (unsigned char)0x30
};

struct Stock
{
	int Quantity, Cost;
};

struct MarketStock
{
	int Quantity;
	int BidPrice; // price clients receive when selling
	int AskPrice; // price clients pay when buying
};

struct Portfolio
{
	int Cash;
	std::unordered_map<std::string, Stock> Stocks; // Stock name: (Quantity, Cost of each)
};

// store info about each connected client
struct ClientInfo {
	SOCKET socket;      // Socket of this client
	uint32_t ip;        // ip address
	uint16_t port;      // port num
	std::string Username;
	Portfolio portfolio;
};

// Global var for the server info
std::vector<ClientInfo> g_clients;					   // container of connected clients
std::mutex              g_clientsMutex;				   // lock for the list
std::unordered_map<std::string , Portfolio> Database;       
std::mutex UsersMutex;
std::unordered_map<std::string, MarketStock> StocksDataBase; // Name of stock: MarketStock struct
std::mutex MarketMutex;


// forward declaration
bool execute(SOCKET clientSocket);
void disconnect(SOCKET& listenerSocket);
void GetLocalIPAddresses();
bool recvAll(SOCKET sock, char* buffer, int length);
bool sendExact(SOCKET sock, const char* buffer, int length);
void addClient(SOCKET sock, uint32_t ip, uint16_t port, const std::string& username);
void removeClient(uint32_t ip, uint16_t port);
void SendError(SOCKET sock, const std::string& msg);
void SendMarket(SOCKET sock);
void BroadcastMarket();
void BuyStock(SOCKET sock, const std::string& username);
void SellStock(SOCKET sock, const std::string& username);
void LogTrade(const std::string& username, const std::string& action,
              const std::string& symbol, int quantity, int unitPrice);

/*
* Logic functions
* Implement trading rules and application behavior
	
	UserLogin
		-Login()
	Stock buy/sell
		-BuyStock()
		-SellStock()
	Viewing of Trade Portfolio/History
		-ViewHistory()
		-ViewPortfolio()
*/

//Logging into the application
void Login(SOCKET sock, std::string const& UserName)
{
	// only 1 client can assess database at a time
	std::lock_guard<std::mutex> lock(UsersMutex);
	Portfolio p;

	auto it = Database.find(UserName);

	// can find, print in client console and gib that info
	if (it != Database.end())
	{
		// get existing user portfolio the from database
		p = it->second;

		unsigned char rsp = RSP_LOGIN;
		int balanceNet = htonl(Database[UserName].Cash);
		sendExact(sock, (char*)&rsp, 1);
		sendExact(sock, (char*)&balanceNet, 4);
	}

	// its a new users, assigned cash value of 10k 
	else
	{
		Database[UserName].Cash = 10000;

		// then send info over.
		unsigned char rsp = RSP_LOGIN;
		int balanceNet = htonl(Database[UserName].Cash);
		sendExact(sock, (char*)&rsp, 1);
		sendExact(sock, (char*)&balanceNet, 4);
	}

}

// Handle client buying stocks: read symbol(8) + quantity(4) from socket
void BuyStock(SOCKET sock, const std::string& username)
{
	// Read the symbol (8 bytes padded) and quantity (4 bytes)
	char symbolBuf[8] = {};
	if (!recvAll(sock, symbolBuf, 8)) return;
	std::string symbol(symbolBuf, strnlen(symbolBuf, 8));

	uint32_t qtyNet;
	if (!recvAll(sock, (char*)&qtyNet, 4)) return;
	int quantity = (int)ntohl(qtyNet);

	if (quantity <= 0)
	{
		SendError(sock, "Invalid quantity.");
		return;
	}

	// Lock both market and user database
	std::lock_guard<std::mutex> mktLock(MarketMutex);
	std::lock_guard<std::mutex> usrLock(UsersMutex);

	// Check stock exists in market
	auto it = StocksDataBase.find(symbol);
	if (it == StocksDataBase.end())
	{
		SendError(sock, "Stock '" + symbol + "' does not exist.");
		return;
	}

	// Check market has enough quantity
	if (it->second.Quantity < quantity)
	{
		SendError(sock, "Not enough shares available. Market has " +
			std::to_string(it->second.Quantity) + " shares of " + symbol + ".");
		return;
	}

	int unitPrice = it->second.AskPrice; // buyers pay the ask price
	int totalCost = unitPrice * quantity;

	// Check user has enough cash
	auto& portfolio = Database[username];
	if (portfolio.Cash < totalCost)
	{
		SendError(sock, "Not enough cash. Need $" + std::to_string(totalCost) +
			" but you have $" + std::to_string(portfolio.Cash) + ".");
		return;
	}

	// Execute the trade
	portfolio.Cash -= totalCost;
	portfolio.Stocks[symbol].Quantity += quantity;
	portfolio.Stocks[symbol].Cost = unitPrice;
	it->second.Quantity -= quantity;

	// Fixed-tick price movement: buying pushes prices up by $1 (min $1)
	const int PRICE_TICK = 1;
	it->second.AskPrice = std::max(1, it->second.AskPrice + PRICE_TICK);
	it->second.BidPrice = std::max(1, it->second.BidPrice + PRICE_TICK);

	// Send RSP_BUY: symbol(8) + quantity(4) + unitPrice(4) + newCash(4)
	unsigned char rsp = RSP_BUY;
	sendExact(sock, (char*)&rsp, 1);

	char symOut[8] = {};
	memcpy(symOut, symbol.c_str(), (symbol.size() < 8 ? symbol.size() : 8));
	sendExact(sock, symOut, 8);

	uint32_t qOut = htonl((uint32_t)quantity);
	sendExact(sock, (char*)&qOut, 4);

	uint32_t pOut = htonl((uint32_t)unitPrice);
	sendExact(sock, (char*)&pOut, 4);

	uint32_t cOut = htonl((uint32_t)portfolio.Cash);
	sendExact(sock, (char*)&cOut, 4);

	// Log the trade
	LogTrade(username, "BUY", symbol, quantity, unitPrice);

	// Broadcast updated market to all clients
	// (release locks first to avoid deadlock - we already made changes)
	// Note: locks are released at end of scope, so broadcast after
}

// Handle client selling stocks: read symbol(8) + quantity(4) from socket
void SellStock(SOCKET sock, const std::string& username)
{
	char symbolBuf[8] = {};
	if (!recvAll(sock, symbolBuf, 8)) return;
	std::string symbol(symbolBuf, strnlen(symbolBuf, 8));

	uint32_t qtyNet;
	if (!recvAll(sock, (char*)&qtyNet, 4)) return;
	int quantity = (int)ntohl(qtyNet);

	if (quantity <= 0)
	{
		SendError(sock, "Invalid quantity.");
		return;
	}

	std::lock_guard<std::mutex> mktLock(MarketMutex);
	std::lock_guard<std::mutex> usrLock(UsersMutex);

	// Check stock exists in market (to get the price)
	auto mktIt = StocksDataBase.find(symbol);
	if (mktIt == StocksDataBase.end())
	{
		SendError(sock, "Stock '" + symbol + "' does not exist.");
		return;
	}

	// Check user has enough of this stock
	auto& portfolio = Database[username];
	auto usrIt = portfolio.Stocks.find(symbol);
	if (usrIt == portfolio.Stocks.end() || usrIt->second.Quantity < quantity)
	{
		int have = (usrIt != portfolio.Stocks.end()) ? usrIt->second.Quantity : 0;
		SendError(sock, "Not enough shares. You have " + std::to_string(have) +
			" shares of " + symbol + ".");
		return;
	}

	int unitPrice = mktIt->second.BidPrice; // sellers receive the bid price
	int totalRevenue = unitPrice * quantity;

	// Execute the trade
	portfolio.Cash += totalRevenue;
	usrIt->second.Quantity -= quantity;
	if (usrIt->second.Quantity == 0)
		portfolio.Stocks.erase(usrIt);
	mktIt->second.Quantity += quantity;

	// Fixed-tick price movement: selling pushes prices down by $1 (min $1)
	constexpr int PRICE_TICK = 1;
	mktIt->second.AskPrice = std::max(1, mktIt->second.AskPrice - PRICE_TICK);
	mktIt->second.BidPrice = std::max(1, mktIt->second.BidPrice - PRICE_TICK);

	// Send RSP_SELL: symbol(8) + quantity(4) + unitPrice(4) + newCash(4)
	unsigned char rsp = RSP_SELL;
	sendExact(sock, (char*)&rsp, 1);

	char symOut[8] = {};
	memcpy(symOut, symbol.c_str(), (symbol.size() < 8 ? symbol.size() : 8));
	sendExact(sock, symOut, 8);

	uint32_t qOut = htonl((uint32_t)quantity);
	sendExact(sock, (char*)&qOut, 4);

	uint32_t pOut = htonl((uint32_t)unitPrice);
	sendExact(sock, (char*)&pOut, 4);

	uint32_t cOut = htonl((uint32_t)portfolio.Cash);
	sendExact(sock, (char*)&cOut, 4);

	LogTrade(username, "SELL", symbol, quantity, unitPrice);
}

// read Trades.txt
void ViewHistory(SOCKET socket)
{
	std::ifstream FilePath("Trades.txt");
	if (!FilePath.is_open())
	{
		std::cout << "Cannot open Trades.txt\n";
		return;
	}

	std::string line;
	std::ostringstream output;
	while (std::getline(FilePath, line))
	{
		output << line;
		output << "\n";
	}

	std::string text = output.str();

	// send RSP_HISTORY to client
	unsigned char rsp = RSP_HISTORY;
	sendExact(socket, (char*)&rsp, 1);

	// send message length to client
	uint32_t msgLen = htonl((uint32_t)text.size());
	sendExact(socket, (char*)&msgLen, 4);

	// send the fake "txt" to client XD. its NOT a file jsut like what would be written to the file but to the client instead :P
	sendExact(socket, text.c_str(), (int)text.size());
}

// View Users own assets
void ViewPortfolio(SOCKET socket, std::string const& Username)
{
	Portfolio portfolio;

	// Only 1 client can use database at a time
	std::lock_guard<std::mutex> lock(UsersMutex);

	portfolio = Database[Username];

	// send RSP_PORTFOLIO to client
	unsigned char rsp = RSP_PORTFOLIO;
	sendExact(socket, (char*)&rsp, 1);

	// one for da cash user has
	uint32_t cash = htonl(portfolio.Cash);
	sendExact(socket, (char*)&cash, 4);

	// Tell client side how many stocks user has
	uint32_t StockCount = htonl((uint32_t)portfolio.Stocks.size());
	sendExact(socket, (char*)&StockCount, 4);

	// one for da stocks user has
	for (auto const& [StockName, Data] : portfolio.Stocks)
	{
		// Pad 2 8 byte, 4 for cost, 4 for quantity
		char symbol[8] = {};
		memcpy(symbol, StockName.c_str(), StockName.size());
		sendExact(socket, symbol, 8);

		uint32_t Quantity = htonl((uint32_t)Data.Quantity);
		sendExact(socket, (char*)&Quantity, 4);

		uint32_t Cost = htonl((uint32_t)Data.Cost);
		sendExact(socket, (char*)&Cost, 4);
	}
}

/*
* Persistence functions
* loading, saving
	
	Database
	-LoadDatabase()
	-SaveDatabase()
	Market
	-LoadMarket()
	-SaveMarket()
	TradeLog
	-LogTrade()
*/


// Load database to the DataBase Container
void LoadDatabase()
{
	// Find file and check if it exist
	std::ifstream FilePath("Users.txt");

	if (!FilePath.is_open())
	{
		std::cout << "Users.txt does not exist :/\n";
		return;
	}

	std::string line;

	// Then we load it into container
	while(std::getline(FilePath, line))
	{
		std::string Username, StockName;
		int Quantity, Cost;
		Portfolio portfolio;	
		std::istringstream in(line);

		in >> Username >> portfolio.Cash;
#ifdef Debug
		std::cout << Username << " " << portfolio.Cash << " ";
#endif

		// grab all the stocks this guys has
		while (in >> StockName >> Quantity >> Cost)
		{
			portfolio.Stocks[StockName] = { Quantity, Cost };

#ifdef Debug
			std::cout << portfolio.Stocks[StockName].first << " " << portfolio.Stocks[StockName].second << std::endl;
#endif
		}

		// this username has all the info we just extracted.
		Database[Username] = portfolio;

	}
}

// Save updated database to Users.txt
void SaveDatabase()
{
	// open Users.txt to save ltr, std::ios::trunc clear the file when u open it :P
	std::ofstream FilePath("Users.txt", std::ios::trunc);

	if (!FilePath.is_open())
	{
		std::cout << "Cannot find file 2 save 2\n";
		return;
	}


	// do for each user we have 
	for (auto& [Username, Portfolio] : Database)
	{
		std::ostringstream write;

		write << Username << " " << Portfolio.Cash;

		// for what stocks theuy have 
		for(auto & [Stocks, Info] : Portfolio.Stocks)
		{
			write << " " << Stocks << " " << Info.Quantity << " " << Info.Cost;
		}

		// so it write s on a new line in the txt
		write << "\n";
		FilePath << write.str();
	}
}

// Load market at the start, read from Stocks.txt
void LoadMarket()
{
	std::ifstream FilePath("Stocks.txt");
	if (!FilePath.is_open())
	{
		std::cout << "Cannot open Stocks.txt\n";
		return;
	}

	std::string line;

	while (std::getline(FilePath,line))
	{
		std::istringstream in(line);
		std::string StockName;
		int Quantity, BidPrice, AskPrice;

		in >> StockName >> Quantity >> BidPrice >> AskPrice;
		StocksDataBase[StockName] = {Quantity, BidPrice, AskPrice};

#ifdef Debug
		std::cout << StocksDataBase[StockName].Quantity << " " << StocksDataBase[StockName].BidPrice << " " << StocksDataBase[StockName].AskPrice << std::endl;
#endif
	}

}

// Save market when client and server closes.
void SaveMarket()
{
	// clear existing stocks.txt and rewrite it
	std::ofstream FilePath("Stocks.txt", std::ios::trunc);
	if(!FilePath.is_open())
	{
		std::cout << "Cannot open Stocks.txt\n";
		return;
	}

	for (auto const& [StockName, Data] : StocksDataBase)
	{
		std::ostringstream write;

		write << StockName << " " << Data.Quantity << " " << Data.BidPrice << " " << Data.AskPrice;
		write << "\n";
		FilePath << write.str();
	}
}

// Log a trade to Trades.txt with timestamp
void LogTrade(const std::string& username, const std::string& action,
	const std::string& symbol, int quantity, int unitPrice)
{
	// Get current time
	auto now = std::chrono::system_clock::now();
	std::time_t t = std::chrono::system_clock::to_time_t(now);
	char timeBuf[64];
	struct tm tmBuf;
	localtime_s(&tmBuf, &t);
	strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tmBuf);

	std::ofstream file("Trades.txt", std::ios::app);
	if (file.is_open())
	{
		file << timeBuf << " " << username << " " << action << " "
			<< symbol << " " << quantity << " @ " << unitPrice
			<< " (total: " << quantity * unitPrice << ")\n";
	}
}

/*
	Networking functions
	Handles connections,sockets, send , receive and client communication

	execute(SOCKET clientSocket)
	disconnect(SOCKET& listenerSocket)
	GetLocalIPAddresses()
	recvAll()
	sendExact()
	addClient()
	removeClient()
	SendError()
	SendMarket()
	BroadcastMarket()

	main()
	-some networking logic e.g. wsa startup, bind socket
*/

int main()
{
	// -------------------------------------------------------------------------
	// Start up Winsock, asking for version 2.2.
	//
	// WSAStartup()
	// -------------------------------------------------------------------------
	// This object holds the information about the version of Winsock that we
	// are using, which is not necessarily the version that we requested.

	// Initialize Winsock. You must call WSACleanup when you are finished.
	WSADATA wsaData{};

	// As this function uses a reference counter, for each call to WSAStartup,
	// you must call WSACleanup or suffer memory issues.
	int errorCode = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (errorCode != NO_ERROR)
	{
		std::cerr << "WSAStartup() failed." << std::endl;
		return errorCode;
	}

	std::cout
		<< "Winsock version: "
		<< static_cast<int>(LOBYTE(wsaData.wVersion))
		<< "."
		<< static_cast<int>(HIBYTE(wsaData.wVersion))
		<< "\n"
		<< std::endl;

	// grab the database and market data and store into mem
	LoadDatabase();
	LoadMarket();

	// Ask for port number:
	std::cout << "Server TCP port Number : ";
	uint16_t Tcp_Port = 0;
	std::cin >> Tcp_Port;
	std::cin.ignore();
	const std::string tcpPortStr = std::to_string(Tcp_Port);

		// Hardcoded example, dont use  
		//constexpr uint16_t port = 2048;
		//const std::string portString = std::to_string(port);

	// -------------------------------------------------------------------------
	// Resolve own host name into IP addresses (in a singly-linked list).
	//
	// getaddrinfo()
	// -------------------------------------------------------------------------

	// Object hints indicates which protocols to use to fill in the info.
	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;			// IPv4

	// For UDP use SOCK_DGRAM instead of SOCK_STREAM.
	hints.ai_socktype = SOCK_STREAM;	// Reliable delivery

	// Could be 0 for autodetect, but reliable delivery over IPv4 is always TCP.
	hints.ai_protocol = IPPROTO_TCP;	// TCP

	// Create a passive socket that is suitable for bind() and listen().
	hints.ai_flags = AI_PASSIVE;

	// Get address info
	addrinfo* info = nullptr;
	errorCode = getaddrinfo(nullptr, tcpPortStr.c_str(), &hints, &info);
	if ((errorCode) || (info == nullptr))
	{
		std::cerr << "getaddrinfo() failed." << std::endl;
		WSACleanup();
		return errorCode;
	}


	// -------------------------------------------------------------------------
	// Create a socket and bind it to own network interface controller.
	//
	// socket()
	// bind()
	// -------------------------------------------------------------------------
	SOCKET listenerSocket = socket(
		hints.ai_family,
		hints.ai_socktype,
		hints.ai_protocol);
	if (listenerSocket == INVALID_SOCKET)
	{
		std::cerr << "socket() failed." << std::endl;
		freeaddrinfo(info);
		WSACleanup();
		return 1;
	}

	// Try and bind
	errorCode = bind(
		listenerSocket,
		info->ai_addr,
		static_cast<int>(info->ai_addrlen));
	if (errorCode != NO_ERROR)
	{
		std::cerr << "bind() failed." << std::endl;
		closesocket(listenerSocket);
		listenerSocket = INVALID_SOCKET;
	}


	if (listenerSocket == INVALID_SOCKET)
	{
		std::cerr << "bind() failed." << std::endl;
		WSACleanup();
		return 2;
	}


	// Get and display socket info
	std::cout << "\nServer IP Address      : ";
	GetLocalIPAddresses();
	std::cout << "Server TCP port Number : " << Tcp_Port << std::endl;

	freeaddrinfo(info);

	// -------------------------------------------------------------------------
	// Set a socket in a listening mode and accept 1 incoming client.
	//
	// listen()
	// accept()
	// -------------------------------------------------------------------------
	// Listen for clients
	errorCode = listen(listenerSocket, SOMAXCONN);
	if (errorCode != NO_ERROR)
	{
		std::cerr << "listen() failed." << std::endl;
		closesocket(listenerSocket);
		WSACleanup();
		return 3;
	}

	// Create TaskQueue and accept clients
	{
		const auto onDisconnect = [&]() { disconnect(listenerSocket); };
		auto tq = TaskQueue<SOCKET, decltype(execute), decltype(onDisconnect)>{ 10, 20, execute, onDisconnect }; // 10 threads + 20 queue slot

		// Main accept loop: runs forever until server shuts down
		while (listenerSocket != INVALID_SOCKET)
		{
			sockaddr clientAddress{};
			SecureZeroMemory(&clientAddress, sizeof(clientAddress));
			int clientAddressSize = sizeof(clientAddress);

			// Accept new client 
			SOCKET clientSocket = accept(
				listenerSocket,
				&clientAddress,
				&clientAddressSize);
			if (clientSocket == INVALID_SOCKET)
			{
				break;
			}
			tq.produce(clientSocket);
		}
	}

	// save data when server close
	SaveDatabase();
	SaveMarket();
	// -------------------------------------------------------------------------
	// Clean-up after Winsock.
	//
	// WSACleanup()
	// -------------------------------------------------------------------------

	WSACleanup();
	return 0;
}

// Send error message to a single client
void SendError(SOCKET sock, const std::string& msg)
{
	unsigned char rsp = RSP_ERROR;
	sendExact(sock, (char*)&rsp, 1);
	uint32_t msgLen = htonl((uint32_t)msg.size());
	sendExact(sock, (char*)&msgLen, 4);
	sendExact(sock, msg.c_str(), (int)msg.size());
}

// Send full market data to a single client
void SendMarket(SOCKET sock)
{
	std::lock_guard<std::mutex> lock(MarketMutex);

	unsigned char rsp = RSP_MARKET;
	sendExact(sock, (char*)&rsp, 1);

	uint32_t stockCount = htonl((uint32_t)StocksDataBase.size());
	sendExact(sock, (char*)&stockCount, 4);

	for (auto const& [StockName, Data] : StocksDataBase)
	{
		char symbol[8] = {};
		memcpy(symbol, StockName.c_str(), (StockName.size() < 8 ? StockName.size() : 8));
		sendExact(sock, symbol, 8);

		uint32_t qty = htonl((uint32_t)Data.Quantity);
		sendExact(sock, (char*)&qty, 4);

		uint32_t bid = htonl((uint32_t)Data.BidPrice);
		sendExact(sock, (char*)&bid, 4);

		uint32_t ask = htonl((uint32_t)Data.AskPrice);
		sendExact(sock, (char*)&ask, 4);
	}
}

// Broadcast market update to ALL connected clients
void BroadcastMarket()
{
	std::lock_guard<std::mutex> clientLock(g_clientsMutex);

	// Build the market payload once (without the per-client send lock)
	unsigned char rsp = RSP_MARKETUPDATE;

	for (auto& client : g_clients)
	{
		// Send RSP_MARKETUPDATE header
		sendExact(client.socket, (char*)&rsp, 1);

		std::lock_guard<std::mutex> mktLock(MarketMutex);
		uint32_t stockCount = htonl((uint32_t)StocksDataBase.size());
		sendExact(client.socket, (char*)&stockCount, 4);

		for (auto const& [StockName, Data] : StocksDataBase)
		{
			char symbol[8] = {};
			memcpy(symbol, StockName.c_str(), (StockName.size() < 8 ? StockName.size() : 8));
			sendExact(client.socket, symbol, 8);

			uint32_t qty = htonl((uint32_t)Data.Quantity);
			sendExact(client.socket, (char*)&qty, 4);

			uint32_t bid = htonl((uint32_t)Data.BidPrice);
			sendExact(client.socket, (char*)&bid, 4);

			uint32_t ask = htonl((uint32_t)Data.AskPrice);
			sendExact(client.socket, (char*)&ask, 4);
		}
	}
}

// Handles one connected client's full session: login, command processing, and cleanup on disconnect.
bool execute(SOCKET clientSocket)
{
	// Get clients ip address and port number from socket
	sockaddr_in clientAddr;
	int addrLen = sizeof(clientAddr);
	getpeername(clientSocket, (sockaddr*)&clientAddr, &addrLen);

	// Store client's IP and Port
	uint32_t clientIP = clientAddr.sin_addr.s_addr;
	uint16_t clientPort = clientAddr.sin_port;

	// Add them to list of clients
	addClient(clientSocket, clientIP, clientPort, "");

	// print client info, then can start command ID
	char ipStr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
	std::cout << "\nNew client connected:" << std::endl;
	std::cout << "Client IP Address = " << ipStr << std::endl;
	std::cout << "Client Port Number = " << ntohs(clientPort) << std::endl << std::endl;
	
	// grab the 1byte command ID the client sent, if fail(client dc), close and end everyhting 
	unsigned char cmdID;
	if (recv(clientSocket, (char*)&cmdID, 1, 0) <= 0)
	{
		removeClient(clientIP, clientPort);
		shutdown(clientSocket, SD_BOTH);
		closesocket(clientSocket);
		return true;
	}

	std::string Username = "";
	if (cmdID == REQ_LOGIN)
	{
		// read name length, fail not get the 4 bytes
		uint32_t NameLength;
		if (!recvAll(clientSocket, (char*)&NameLength, 4))
		{
			removeClient(clientIP, clientPort);
			shutdown(clientSocket, SD_BOTH);
			closesocket(clientSocket);
			return true;
		}

		// Convert from network to host byte order
		NameLength = ntohl(NameLength);

		// Make sure that if they give us no username that they can FUCK OFF :>
		if (NameLength == 0)
		{
			removeClient(clientIP, clientPort);
			shutdown(clientSocket, SD_BOTH);
			closesocket(clientSocket);
			return true;
		}

		// read username
	    Username = std::string(NameLength, '\0');
		if (!recvAll(clientSocket, Username.data(), NameLength))
		{
			removeClient(clientIP, clientPort);
			shutdown(clientSocket, SD_BOTH);
			closesocket(clientSocket);
			return true;
		}

		Login(clientSocket, Username);

		// Update client info with username
		{
			std::lock_guard<std::mutex> lock(g_clientsMutex);
			for (auto& c : g_clients)
			{
				if (c.ip == clientIP && c.port == clientPort)
				{
					c.Username = Username;
					break;
				}
			}
		}

		// Send initial market data to the client
		SendMarket(clientSocket);
	}


	// Main loop for command ID
	while (true)
	{
		unsigned char commandID;
		const int bytesReceived = recv(
			clientSocket,
			(char*)&commandID,
			1,
			0);

		if (bytesReceived == SOCKET_ERROR)
		{
			std::cerr << "recv() failed." << std::endl;
			break;
		}
		if (bytesReceived == 0)
		{
			std::cout << "Graceful shutdown." << std::endl;
			break;
		}

		if (commandID == REQ_BUY) // Buying stocks "/b"
		{
			BuyStock(clientSocket, Username);
			BroadcastMarket();
		}
		else if (commandID == REQ_SELL) // Selling stocks "/s"
		{
			SellStock(clientSocket, Username);
			BroadcastMarket();
		}

		else if (commandID == REQ_MARKET) // View market "/m"
		{
			SendMarket(clientSocket);
		}

		else if (commandID == REQ_PORTFOLIO) // To view portfolio "/p"
		{
			ViewPortfolio(clientSocket, Username);
		}

		else if (commandID == REQ_HISTORY) // Show stock history "/h"
		{
			ViewHistory(clientSocket);
		}

		else if (commandID == REQ_QUIT)		// "/q"
		{
			break;  
		}
		else
		{
			// nth needs to be printed
		}
	}

	SaveDatabase();
	SaveMarket();
	// -------------------------------------------------------------------------
	// Shut down , remove client and close sockets.
	//
	// removeClient()
	// shutdown()
	// closesocket()
	// -------------------------------------------------------------------------

	removeClient(clientIP, clientPort);
	shutdown(clientSocket, SD_BOTH);
	closesocket(clientSocket);
	return true;
}

//disconnect the socket
void disconnect(SOCKET& listenerSocket)
{
	if (listenerSocket != INVALID_SOCKET)
	{
		shutdown(listenerSocket, SD_BOTH);
		closesocket(listenerSocket);
		listenerSocket = INVALID_SOCKET;
	}
}

//prints the machine's local IP addresses
void GetLocalIPAddresses()
{
	char hostname[256];
	gethostname(hostname, sizeof(hostname));

	struct addrinfo hints = {};
	hints.ai_family = AF_INET; // IPv4 only
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo* result = nullptr;
	if (getaddrinfo(hostname, nullptr, &hints, &result) == 0) {
		for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
			struct sockaddr_in* sockaddr_ipv4 = (struct sockaddr_in*)ptr->ai_addr;
			char ipStr[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &sockaddr_ipv4->sin_addr, ipStr, sizeof(ipStr));

			// Skip loopback address
			if (strcmp(ipStr, "127.0.0.1") != 0) {
				std::cout << ipStr << std::endl;
			}
		}
		freeaddrinfo(result);
	}
}

// Function to receive exact number of bytes (handles partial receives)
bool recvAll(SOCKET sock, char* buffer, int length) {
	int totalReceived = 0;

	while (totalReceived < length) {
		int received = recv(sock, buffer + totalReceived, length - totalReceived, 0);

		if (received <= 0)
		{
			return false; // Connection closed or error
		}

		totalReceived += received;
	}

	return true;
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

// Add client to global list
void addClient(SOCKET sock, uint32_t ip, uint16_t port, const std::string& username) {
	std::lock_guard<std::mutex> lock(g_clientsMutex);

	ClientInfo client;
	client.socket = sock;
	client.ip = ip;
	client.port = port;
	client.Username = username;

	g_clients.push_back(client); // add to contianer
}

// Remove client from global list  
void removeClient(uint32_t ip, uint16_t port) {
	std::lock_guard<std::mutex> lock(g_clientsMutex);

	for (size_t i = 0; i < g_clients.size(); ++i)
	{
		if (g_clients[i].ip == ip && g_clients[i].port == port)
		{
			g_clients.erase(g_clients.begin() + i);
			break;
		}
	}
}