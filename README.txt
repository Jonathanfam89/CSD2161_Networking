/******************************************************************************/ 
/*!
\file        README.txt
\author      Ban Kai Wei Benjamin
\par         k.ban@digipen.edu
\author      Chia Wei Xuan, Rachael
\par         c.weixuanrachael@digipen.edu
\author      Fam Qian Wei Jonathan
\par	     fam.q@digipen.edu      
\author      Zong Han Law
\par         zonghan.law@digipen.edu
\author      Raziq
\par         

\date        28 Mar 2026
\brief       CSD2161/CSD2160:Assignment 5 – 
		Interactive Online Trading Application

Copyright (C) 2026 DigiPen Institute of Technology.

Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited.
*/
/******************************************************************************/

README
======
This project is a client-server based Interactive Online Trading Application
developed in C++ using Winsock.

The server is multithreaded and uses the task queue / worker-thread model for handling multiple TCP clients. 
Given in Assignment 3.
 
The application allows multiple clients to connect to a central server and
perform stock trading related actions in real time. Users may log in, view
market information, buy and sell stocks, inspect their portfolio, and review
stock history. The server manages connected clients, processes commands, and
keeps market and user data updated.

2. FEATURES
-----------
- Client-server communication over TCP
- Multiple client support
- User login with username
- View live market information
- Buy stocks
- Sell stocks
- View personal portfolio
- View stock price history
- Graceful client disconnect handling
- Saving market and database data

3. PROGRAM STRUCTURE
--------------------
The program is split into two main components:

A. Server
   - Accepts incoming client connections
   - Handles user login
   - Processes client requests
   - Updates market and portfolio data
   - Broadcasts updated market information when needed
   - Saves persistent data before shutdown or disconnect

B. Client
   - Connects to the server
   - Sends command requests
   - Displays server responses to the user
   - Allows interaction with the trading system through commands

Contains Files
Source Files
-Server.cpp
-Client.cpp
-taskqueue.h
-taskqueue.hpp

Data Files
-Users.txt
-Trades.txt
-Stocks.txt

Executable Files
-Server.exe
-Client.exe

4. COMMANDS
-----------
The following commands are supported by the application:

/b          Buy stocks
/s          Sell stocks
/p          View portfolio
/h          View trade history
/m          View market
/q          Quit application

5. HOW TO BUILD
---------------
Requirements:
- Microsoft Visual Studio 2022
- Windows operating system
- Winsock2 library

Steps:
1. Open the solution file in Visual Studio 2022
2. Ensure all source files are included in the project
3. Set Server as Start Up Project and build to get Server.exe
3. Then Set Client as Start Up Project and build to get Client.exe

6. HOW TO RUN
-------------
1. Ensure Text files are in same Directory as Server.exe and Client.exe
2. Launch the server application (Server.exe)
3. Enter Server TCP port Number, the server will be running once entered
4. Launch the client application (Client.exe.
5. Enter the Server IP address shown on the (Server.exe) application
6. Enter the Server TCP Port number shown on the (Server.exe) application
7. Enter Username once connected to the server.
8. User's Account balance, Market Data and commands are shown on console.

