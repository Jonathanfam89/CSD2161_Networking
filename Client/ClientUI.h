/*!*****************************************************************************
\file   ClientUI.h
\brief  ImGui UI declarations for the trading client.

Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*******************************************************************************/
#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <cfloat>
#include <chrono>
#include <ctime>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui/imgui.h"

//  Data Structures 
struct StockInfo {
    std::string symbol;
    int         quantity;
    int         bid;
    int         ask;
};

struct HoldingInfo {
    std::string symbol;
    int         quantity;
    int         cost;
};

struct LogEntry {
    std::string message;
    ImVec4      color;
};

static const int MAX_HISTORY_POINTS = 604800;
struct PriceHistory {
    std::vector<float>  bidHistory;
    std::vector<float>  askHistory;
    std::vector<double> timestamps;
};

struct GraphViewState {
    double viewDuration = 60.0;
    double viewEnd      = 0.0;
    bool   dragging     = false;
    float  dragStartX   = 0.0f;
    double dragStartEnd = 0.0;
};

//  Mutable UI widget state (owned by WinMain, passed into render) 
struct UIState {
    char   serverIP[64]   = "127.0.0.1";
    char   serverPort[16] = "8888";
    char   username[64]   = "";
    char   buySymbol[16]  = "";
    int    buyQty         = 1;
    char   sellSymbol[16] = "";
    int    sellQty        = 1;
};

//  Extern shared state (defined in Client.cpp) 
extern std::mutex              g_stateMutex;
extern volatile bool           g_running;
extern bool                    g_connected;
extern bool                    g_loggedIn;
extern int                     g_cash;
extern std::vector<StockInfo>  g_market;
extern std::vector<HoldingInfo>g_portfolio;
extern std::string             g_tradeHistory;
extern std::deque<LogEntry>    g_log;
extern SOCKET                  g_tcpSock;
extern std::unordered_map<std::string, PriceHistory>  g_priceHistory;
extern std::unordered_map<std::string, GraphViewState> g_graphViews;

//  Network send helpers (defined in Client.cpp) 
void AddLog(const std::string& msg, ImVec4 color = ImVec4(1, 1, 1, 1));
void SendLogin(const std::string& username);
void SendBuy(const std::string& symbol, int quantity);
void SendSell(const std::string& symbol, int quantity);
void SendPortfolioRequest();
void SendHistoryRequest();
void SendMarketRequest();
void SendQuit();

//  UI Render Functions (defined in ClientUI.cpp) 

// Returns true if the user clicked Connect & Login and connection succeeded.
// Caller must then start the receiver thread.
bool RenderLoginScreen(UIState& ui, ImGuiIO& io);

void RenderTradingUI(UIState& ui, ImGuiIO& io);
