================================================================================
CSD2161 Assignment 5 - Online Trading Platform (Option 3)
================================================================================

OVERVIEW
--------
A client-server online stock trading system over TCP/LAN. Multiple clients can
connect simultaneously to buy/sell stocks from a shared market. All state is
server-authoritative with persistent storage.

Matching Model: Market-maker quotes (fixed bid/ask prices per stock).
Stocks have a fixed unit price set in Stocks.txt. Clients buy/sell at that price.


BUILD
-----
- Open CSD2161_A5.sln in Visual Studio 2022
- Build in Release mode, x64 platform
- Outputs: x64/Release/Server.exe, x64/Release/Client.exe


HOW TO RUN
----------
1. Start the Server:
   - Run Server.exe
   - Enter a TCP port number (e.g. 8888)
   - Server displays its IP address and begins listening

2. Start one or more Clients:
   - Run Client.exe
   - Enter the server IP address shown by the server
   - Enter the same TCP port number (e.g. 8888)
   - Enter a username (new users get $10,000 starting balance)
   - Market data is displayed automatically on login

3. Use client commands:
   /b  - Buy stocks   (prompts for symbol and quantity)
   /s  - Sell stocks   (prompts for symbol and quantity)
   /p  - View your portfolio (cash + holdings)
   /h  - View trade history (all trades by all users)
   /m  - Refresh market data display
   /q  - Quit

4. To stop the server, close the server console window.
   User data and market state are saved on each client disconnect.


DATA FILES (Server directory)
-----------------------------
- Stocks.txt   : Market stock data. Format: StockName Quantity Price
                  (one stock per line)
- Users.txt    : User accounts. Format: Username Cash [StockName Qty Price ...]
                  (one user per line, stocks appended after cash)
- Trades.txt   : Trade audit log. Timestamped records of all buy/sell trades.

Default stocks: AAPL, GOOG, MSFT, AMZN, TSLA
New users start with $10,000 cash.


EXAMPLE SESSION
---------------
Server:
  > Server TCP port Number : 8888
  > Server IP Address      : 192.168.1.10
  > Server TCP port Number : 8888

Client:
  > Server IP Address      : 192.168.1.10
  > Server TCP Port Number : 8888
  > Connected to the server!
  > Username: alice
  > Login successful!
  > Account Balance: $10000
  >
  > ===== MARKET DATA =====
  > Symbol   | Available |   Price
  > ---------+-----------+--------
  > AAPL     |      1000 | $150
  > GOOG     |       500 | $280
  > MSFT     |       800 | $310
  > AMZN     |       600 | $175
  > TSLA     |       400 | $240
  > =======================
  >
  > /b
  > Stock symbol: AAPL
  > Quantity: 5
  > BUY SUCCESS: 5 x AAPL @ $150 each (total: $750)
  > Remaining Balance: $9250


MULTI-CLIENT TESTING
--------------------
1. Start Server.exe
2. Open multiple Client.exe instances (on same or different machines on LAN)
3. Log in with different usernames
4. Buy/sell stocks from any client - market updates broadcast to all clients
5. Use /p to verify portfolio consistency
6. Use /h to see the full trade audit trail


PROTOCOL FORMAT
---------------
All messages use a binary protocol over TCP with network byte order (big-endian).

  Command ID (1 byte) - identifies the message type
  Followed by command-specific payload fields

Command IDs:
  0x01  REQ_QUIT       : Client disconnect (no payload)
  0x02  REQ_LOGIN      : NameLength(4) + Username(N)
  0x03  RSP_LOGIN      : Balance(4)
  0x04  REQ_BUY        : Symbol(8) + Quantity(4)
  0x05  RSP_BUY        : Symbol(8) + Quantity(4) + UnitPrice(4) + NewCash(4)
  0x06  REQ_SELL       : Symbol(8) + Quantity(4)
  0x07  RSP_SELL       : Symbol(8) + Quantity(4) + UnitPrice(4) + NewCash(4)
  0x08  REQ_PORTFOLIO  : (no payload)
  0x09  RSP_PORTFOLIO  : Cash(4) + StockCount(4) + [Symbol(8)+Qty(4)+Cost(4)]...
  0x10  REQ_HISTORY    : (no payload)
  0x11  RSP_HISTORY    : MsgLength(4) + Text(N)
  0x12  RSP_MARKET     : StockCount(4) + [Symbol(8) + Qty(4) + Price(4)]...
  0x13  RSP_MARKETUPDATE : (same format as RSP_MARKET, broadcast to all)
  0x14  REQ_MARKET     : (no payload, requests market refresh)
  0x25  RSP_ERROR      : MsgLength(4) + ErrorText(N)

Partial-read handling: recvAll() / sendExact() ensure complete message delivery.


ARCHITECTURE
------------
Server:
  - Multi-threaded via TaskQueue (producer-consumer thread pool, 10 workers)
  - Each client handled in its own worker thread
  - Shared state protected by mutexes (UsersMutex, MarketMutex, g_clientsMutex)
  - Persistent storage via text files (Users.txt, Stocks.txt, Trades.txt)

Client:
  - Main thread handles user input and sends requests
  - Dedicated receiver thread processes all server responses asynchronously
  - Thread-safe console output via g_outputMutex
