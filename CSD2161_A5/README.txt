================================================================================
CSD2161 Assignment 5 - Online Trading Platform (Option 3)
================================================================================

OVERVIEW
--------
A client-server online stock trading system over TCP/LAN. Multiple clients can
connect simultaneously to buy/sell stocks from a shared market. All state is
server-authoritative with persistent storage.

The client features a graphical UI built with ImGui (DirectX 11 backend).

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

Build modes:
  - Debug (x64)   : Client runs with a console window (for debug output)
                     alongside the ImGui GUI window.
  - Release (x64) : Client runs with the ImGui GUI window only (no console).


HOW TO RUN
----------
1. Start the Server:
   - Run Server.exe
   - Enter a TCP port number (e.g. 8888)
   - Server displays its IP address and begins listening

2. Start one or more Clients:
   - Run Client.exe
   - A login window appears - enter the server IP, port, and username
   - Click "Connect & Login" (new users get $10,000 starting balance)
   - Market data is displayed automatically on login

3. Client GUI:
   - Market panel (left)     : live table of stocks with Bid/Ask prices;
                                click "Refresh" to update
   - Activity Log (left)     : color-coded log of trades, errors, notifications
   - Trading panel (right)   : collapsible sections for:
       Portfolio              : cash balance + holdings table
       Buy Stocks             : enter symbol + quantity, click "Buy"
       Sell Stocks            : enter symbol + quantity, click "Sell"
       Trade History          : click "Load History" to view all past trades
   - Top bar                 : shows username, cash, and "Disconnect" button

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


MULTI-CLIENT TESTING
--------------------
1. Start Server.exe
2. Open multiple Client.exe instances (on same or different machines on LAN)
3. Log in with different usernames
4. Buy/sell stocks from any client - market updates broadcast to all clients
   automatically, and prices shift with each trade
5. Check the Portfolio section to verify consistency
6. Expand Trade History to see the full audit trail


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
  - ImGui (Dear ImGui) graphical interface with DirectX 11 rendering backend
  - Main thread runs the ImGui render loop, reads shared state, sends commands
  - Dedicated receiver thread processes all server responses asynchronously
    and updates shared state (mutex-protected)
  - Debug build includes a console window for stdout/stderr output
