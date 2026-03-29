================================================================================
CSD2161 Assignment 5 - Online Trading Platform (Option 3)
================================================================================

OVERVIEW
--------
A client-server online stock trading system over TCP/LAN. Multiple clients can
connect simultaneously to buy/sell stocks from a shared market. All state is
server-authoritative with persistent storage.

Matching Model: Market-maker quotes with bid/ask spread.
  - Ask price: the price clients pay when BUYING.
  - Bid price: the price clients receive when SELLING.
  - Prices move dynamically: buying pushes prices up by $1, selling pushes
    prices down by $1 (minimum $1).


BUILD
-----
- Open CSD2161_A5.sln in Visual Studio 2022
- Build in Release mode, x64 platform
- Outputs: x64/Release/Server.exe, x64/Release/Client.exe
- Data files (Stocks.txt, Users.txt, Trades.txt) are automatically copied
  next to Server.exe via a post-build event.
- Run clean.bat to remove all build artifacts.


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
   /b  - Buy stocks   (prompts for symbol and quantity, pays Ask price)
   /s  - Sell stocks   (prompts for symbol and quantity, receives Bid price)
   /p  - View your portfolio (cash + holdings)
   /h  - View trade history (all trades by all users)
   /m  - Refresh market data display
   /q  - Quit

4. To stop the server, close the server console window.
   User data and market state are saved on each client disconnect.


DATA FILES (Server directory)
-----------------------------
- Stocks.txt   : Market stock data.
                  Format: StockName Quantity BidPrice AskPrice
                  (one stock per line)
- Users.txt    : User accounts.
                  Format: Username Cash [StockName Qty Cost ...]
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
  > Symbol   | Available |    Bid  |    Ask
  > ---------+-----------+---------+--------
  > AAPL     |      1000 | $   146 | $154
  > GOOG     |       500 | $   272 | $288
  > MSFT     |       800 | $   301 | $319
  > AMZN     |       600 | $   170 | $180
  > TSLA     |       400 | $   233 | $247
  > =======================
  >
  > /b
  > Stock symbol: AAPL
  > Quantity: 5
  > BUY SUCCESS: 5 x AAPL @ $154 each (total: $770)
  > Remaining Balance: $9230


MULTI-CLIENT TESTING
--------------------
1. Start Server.exe
2. Open multiple Client.exe instances (on same or different machines on LAN)
3. Log in with different usernames
4. Buy/sell stocks from any client - market updates broadcast to all clients
   automatically, and prices shift with each trade
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
  0x12  RSP_MARKET     : StockCount(4) + [Symbol(8)+Qty(4)+Bid(4)+Ask(4)]...
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
  - Market-maker pricing: bid/ask spread with dynamic $1 tick movement per trade

Client:
  - Main thread handles user input and sends requests
  - Dedicated receiver thread processes all server responses asynchronously
  - Thread-safe console output via g_outputMutex
