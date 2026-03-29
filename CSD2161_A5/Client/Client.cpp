/*!*****************************************************************************
\file   Client.cpp
\author Rachael Chia (c.weixuanrachael@digipen.edu)
\author Ban Kai Wei Benjamin (k.ban@digipen.edu)
\author Fam Qian Wei jonathan (fam.q@digipen.edu)
\author Zong Han Law (zonghan.law@digipen.edu)
\author Muhammad Raziq (muhammadraziq.b@digipen.edu)

\date   03/28/2026
\par Course: CSD2161
\par Assignment: Assignment 5
\brief  This file is the Client-side implementation of a TCP LAN
        online trading platform.

        Sends user requests to the server and receives account, market,
        portfolio, and trade-history updates.
        
        Online Trading Platform Client with ImGui UI (DirectX 11)
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*******************************************************************************/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <ws2tcpip.h>
#include <d3d11.h>
#include <dxgi.h>

#include <mutex>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <deque>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui/imgui.h"
#include "imgui/imgui.cpp"
#include "imgui/imgui_draw.cpp"
#include "imgui/imgui_tables.cpp"
#include "imgui/imgui_widgets.cpp"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_win32.cpp"
#include "imgui/backends/imgui_impl_dx11.h"
#include "imgui/backends/imgui_impl_dx11.cpp"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// ── Protocol Command IDs ────────────────────────────────────────────────────
enum CMDID : unsigned char {
    UNKNOWN          = 0x00,
    REQ_QUIT         = 0x01,
    REQ_LOGIN        = 0x02,
    RSP_LOGIN        = 0x03,
    REQ_BUY          = 0x04,
    RSP_BUY          = 0x05,
    REQ_SELL         = 0x06,
    RSP_SELL         = 0x07,
    REQ_PORTFOLIO    = 0x08,
    RSP_PORTFOLIO    = 0x09,
    REQ_HISTORY      = 0x10,
    RSP_HISTORY      = 0x11,
    RSP_MARKET       = 0x12,
    RSP_MARKETUPDATE = 0x13,
    REQ_MARKET       = 0x14,
    CMD_TEST         = 0x20,
    RSP_ERROR        = 0x25,
    ECHO_ERROR       = 0x30
};

// ── Shared Application State ────────────────────────────────────────────────
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

// Global shared state
std::mutex              g_stateMutex;
volatile bool           g_running       = true;
bool                    g_connected     = false;
bool                    g_loggedIn      = false;
int                     g_cash          = 0;
std::vector<StockInfo>  g_market;
std::vector<HoldingInfo>g_portfolio;
std::string             g_tradeHistory;
std::deque<LogEntry>    g_log;
SOCKET                  g_tcpSock       = INVALID_SOCKET;

void AddLog(const std::string& msg, ImVec4 color = ImVec4(1, 1, 1, 1))
{
    std::lock_guard<std::mutex> lk(g_stateMutex);
    g_log.push_back({msg, color});
    if (g_log.size() > 200) g_log.pop_front();
}

// ── Network Helpers ─────────────────────────────────────────────────────────
int recvAll(SOCKET sock, char* buffer, int length)
{
    int total = 0;
    while (total < length) {
        int n = recv(sock, buffer + total, length - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

bool sendExact(SOCKET sock, const char* buffer, int length)
{
    int totalSent = 0;
    while (totalSent < length) {
        int result = send(sock, buffer + totalSent, length - totalSent, 0);
        if (result == SOCKET_ERROR) return false;
        totalSent += result;
    }
    return true;
}

// ── Network Send Commands ───────────────────────────────────────────────────
void SendLogin(const std::string& username)
{
    unsigned char cmd = REQ_LOGIN;
    uint32_t nameLen = htonl((uint32_t)username.size());
    sendExact(g_tcpSock, (char*)&cmd, 1);
    sendExact(g_tcpSock, (char*)&nameLen, 4);
    sendExact(g_tcpSock, username.c_str(), (int)username.size());
}

void SendBuy(const std::string& symbol, int quantity)
{
    unsigned char cmd = REQ_BUY;
    sendExact(g_tcpSock, (char*)&cmd, 1);
    char symBuf[8] = {};
    memcpy(symBuf, symbol.c_str(), (symbol.size() < 8 ? symbol.size() : 8));
    sendExact(g_tcpSock, symBuf, 8);
    uint32_t qtyNet = htonl((uint32_t)quantity);
    sendExact(g_tcpSock, (char*)&qtyNet, 4);
}

void SendSell(const std::string& symbol, int quantity)
{
    unsigned char cmd = REQ_SELL;
    sendExact(g_tcpSock, (char*)&cmd, 1);
    char symBuf[8] = {};
    memcpy(symBuf, symbol.c_str(), (symbol.size() < 8 ? symbol.size() : 8));
    sendExact(g_tcpSock, symBuf, 8);
    uint32_t qtyNet = htonl((uint32_t)quantity);
    sendExact(g_tcpSock, (char*)&qtyNet, 4);
}

void SendPortfolioRequest()
{
    unsigned char cmd = REQ_PORTFOLIO;
    sendExact(g_tcpSock, (char*)&cmd, 1);
}

void SendHistoryRequest()
{
    unsigned char cmd = REQ_HISTORY;
    sendExact(g_tcpSock, (char*)&cmd, 1);
}

void SendMarketRequest()
{
    unsigned char cmd = REQ_MARKET;
    sendExact(g_tcpSock, (char*)&cmd, 1);
}

void SendQuit()
{
    unsigned char cmd = REQ_QUIT;
    send(g_tcpSock, (char*)&cmd, 1, 0);
    g_running = false;
}

// ── Receiver Thread ─────────────────────────────────────────────────────────
void receiverThread()
{
    while (g_running)
    {
        unsigned char cmdID;
        if (recv(g_tcpSock, (char*)&cmdID, 1, 0) <= 0) {
            AddLog("Disconnected from server.", ImVec4(1, 0.3f, 0.3f, 1));
            g_running = false;
            break;
        }

        if (cmdID == RSP_LOGIN)
        {
            int NetBalance;
            if (recvAll(g_tcpSock, (char*)&NetBalance, 4) <= 0) break;
            int balance = ntohl(NetBalance);

            {
                std::lock_guard<std::mutex> lk(g_stateMutex);
                g_cash = balance;
                g_loggedIn = true;
            }
            AddLog("Login successful! Balance: $" + std::to_string(balance),
                   ImVec4(0.3f, 1, 0.3f, 1));
        }
        else if (cmdID == RSP_MARKET || cmdID == RSP_MARKETUPDATE)
        {
            uint32_t stockCountNet;
            if (recvAll(g_tcpSock, (char*)&stockCountNet, 4) <= 0) break;
            uint32_t stockCount = ntohl(stockCountNet);

            std::vector<StockInfo> newMarket;
            for (uint32_t i = 0; i < stockCount; ++i)
            {
                char sym[8] = {};
                if (recvAll(g_tcpSock, sym, 8) <= 0) break;
                uint32_t qtyNet, bidNet, askNet;
                if (recvAll(g_tcpSock, (char*)&qtyNet, 4) <= 0) break;
                if (recvAll(g_tcpSock, (char*)&bidNet, 4) <= 0) break;
                if (recvAll(g_tcpSock, (char*)&askNet, 4) <= 0) break;

                StockInfo si;
                si.symbol = std::string(sym, strnlen(sym, 8));
                si.quantity = (int)ntohl(qtyNet);
                si.bid = (int)ntohl(bidNet);
                si.ask = (int)ntohl(askNet);
                newMarket.push_back(si);
            }

            std::lock_guard<std::mutex> lk(g_stateMutex);
            g_market = newMarket;
        }
        else if (cmdID == RSP_BUY)
        {
            char sym[8] = {};
            if (recvAll(g_tcpSock, sym, 8) <= 0) break;
            uint32_t qtyNet, priceNet, cashNet;
            if (recvAll(g_tcpSock, (char*)&qtyNet, 4) <= 0) break;
            if (recvAll(g_tcpSock, (char*)&priceNet, 4) <= 0) break;
            if (recvAll(g_tcpSock, (char*)&cashNet, 4) <= 0) break;

            std::string symbol(sym, strnlen(sym, 8));
            int qty = (int)ntohl(qtyNet);
            int price = (int)ntohl(priceNet);
            int cash = (int)ntohl(cashNet);

            {
                std::lock_guard<std::mutex> lk(g_stateMutex);
                g_cash = cash;
            }

            AddLog("BUY: " + std::to_string(qty) + " x " + symbol +
                   " @ $" + std::to_string(price) + " (total: $" +
                   std::to_string(qty * price) + ") | Balance: $" +
                   std::to_string(cash),
                   ImVec4(0.3f, 1, 0.3f, 1));

            SendPortfolioRequest();
        }
        else if (cmdID == RSP_SELL)
        {
            char sym[8] = {};
            if (recvAll(g_tcpSock, sym, 8) <= 0) break;
            uint32_t qtyNet, priceNet, cashNet;
            if (recvAll(g_tcpSock, (char*)&qtyNet, 4) <= 0) break;
            if (recvAll(g_tcpSock, (char*)&priceNet, 4) <= 0) break;
            if (recvAll(g_tcpSock, (char*)&cashNet, 4) <= 0) break;

            std::string symbol(sym, strnlen(sym, 8));
            int qty = (int)ntohl(qtyNet);
            int price = (int)ntohl(priceNet);
            int cash = (int)ntohl(cashNet);

            {
                std::lock_guard<std::mutex> lk(g_stateMutex);
                g_cash = cash;
            }

            AddLog("SELL: " + std::to_string(qty) + " x " + symbol +
                   " @ $" + std::to_string(price) + " (total: $" +
                   std::to_string(qty * price) + ") | Balance: $" +
                   std::to_string(cash),
                   ImVec4(1, 0.8f, 0.3f, 1));

            SendPortfolioRequest();
        }
        else if (cmdID == RSP_PORTFOLIO)
        {
            uint32_t cashNet, countNet;
            if (recvAll(g_tcpSock, (char*)&cashNet, 4) <= 0) break;
            if (recvAll(g_tcpSock, (char*)&countNet, 4) <= 0) break;
            int cash = (int)ntohl(cashNet);
            uint32_t count = ntohl(countNet);

            std::vector<HoldingInfo> newPortfolio;
            for (uint32_t i = 0; i < count; ++i)
            {
                char sym[8] = {};
                if (recvAll(g_tcpSock, sym, 8) <= 0) break;
                uint32_t qtyNet, costNet;
                if (recvAll(g_tcpSock, (char*)&qtyNet, 4) <= 0) break;
                if (recvAll(g_tcpSock, (char*)&costNet, 4) <= 0) break;

                HoldingInfo hi;
                hi.symbol = std::string(sym, strnlen(sym, 8));
                hi.quantity = (int)ntohl(qtyNet);
                hi.cost = (int)ntohl(costNet);
                newPortfolio.push_back(hi);
            }

            std::lock_guard<std::mutex> lk(g_stateMutex);
            g_cash = cash;
            g_portfolio = newPortfolio;
        }
        else if (cmdID == RSP_HISTORY)
        {
            uint32_t msgLenNet;
            if (recvAll(g_tcpSock, (char*)&msgLenNet, 4) <= 0) break;
            uint32_t msgLen = ntohl(msgLenNet);

            std::string history(msgLen, '\0');
            if (msgLen > 0) {
                if (recvAll(g_tcpSock, &history[0], (int)msgLen) <= 0) break;
            }

            std::lock_guard<std::mutex> lk(g_stateMutex);
            g_tradeHistory = history;
        }
        else if (cmdID == RSP_ERROR)
        {
            uint32_t msgLenNet;
            if (recvAll(g_tcpSock, (char*)&msgLenNet, 4) <= 0) break;
            uint32_t msgLen = ntohl(msgLenNet);
            std::string msg(msgLen, '\0');
            if (recvAll(g_tcpSock, &msg[0], msgLen) <= 0) break;

            AddLog("ERROR: " + msg, ImVec4(1, 0.3f, 0.3f, 1));
        }
    }
}

// ── DirectX 11 Globals ─────────────────────────────────────────────────────
static ID3D11Device*           g_pd3dDevice           = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext     = nullptr;
static IDXGISwapChain*         g_pSwapChain           = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static bool                    g_resizeRequested      = false;
static UINT                    g_resizeWidth          = 0;
static UINT                    g_resizeHeight         = 0;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ── Console entry point for Debug builds ────────────────────────────────────
#ifdef _DEBUG
int main()
{
    return WinMain(GetModuleHandle(nullptr), nullptr, GetCommandLineA(), SW_SHOWDEFAULT);
}
#endif

// ── Main ────────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    // Init Winsock
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != NO_ERROR) {
        MessageBoxA(nullptr, "WSAStartup failed", "Error", MB_OK);
        return 1;
    }

    // Create window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0,
                       hInstance, nullptr, nullptr, nullptr, nullptr,
                       L"TradingClient", nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Online Trading Platform",
                              WS_OVERLAPPEDWINDOW, 100, 100, 1280, 720,
                              nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // Customize style
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // UI state
    char serverIP[64]   = "127.0.0.1";
    char serverPort[16] = "8888";
    char username[64]   = "";
    char buySymbol[16]  = "";
    int  buyQty         = 1;
    char sellSymbol[16] = "";
    int  sellQty        = 1;
    std::thread receiverThr;
    ImVec4 clearColor = ImVec4(0.06f, 0.06f, 0.1f, 1.0f);

    // Main loop
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        // Handle resize
        if (g_resizeRequested) {
            g_resizeRequested = false;
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_resizeWidth, g_resizeHeight,
                                        DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }

        // Start ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // ── LOGIN SCREEN ────────────────────────────────────────────────
        if (!g_connected)
        {
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f,
                                           io.DisplaySize.y * 0.5f),
                                    ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(400, 220));
            ImGui::Begin("Connect to Server", nullptr,
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

            ImGui::Text("Online Trading Platform");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::InputText("Server IP", serverIP, sizeof(serverIP));
            ImGui::InputText("Port", serverPort, sizeof(serverPort));
            ImGui::InputText("Username", username, sizeof(username));

            ImGui::Spacing();
            if (ImGui::Button("Connect & Login", ImVec2(-1, 30)))
            {
                if (strlen(username) > 0)
                {
                    g_tcpSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                    sockaddr_in srvAddr{};
                    srvAddr.sin_family = AF_INET;
                    srvAddr.sin_port = htons((u_short)atoi(serverPort));
                    inet_pton(AF_INET, serverIP, &srvAddr.sin_addr);

                    if (connect(g_tcpSock, (sockaddr*)&srvAddr, sizeof(srvAddr))
                        == SOCKET_ERROR)
                    {
                        AddLog("Connection failed: " +
                               std::to_string(WSAGetLastError()),
                               ImVec4(1, 0.3f, 0.3f, 1));
                        closesocket(g_tcpSock);
                        g_tcpSock = INVALID_SOCKET;
                    }
                    else
                    {
                        g_connected = true;
                        g_running = true;
                        SendLogin(username);
                        receiverThr = std::thread(receiverThread);
                    }
                }
            }

            // Show connection errors
            {
                std::lock_guard<std::mutex> lk(g_stateMutex);
                for (auto& entry : g_log) {
                    ImGui::TextColored(entry.color, "%s", entry.message.c_str());
                }
            }

            ImGui::End();
        }
        // ── MAIN TRADING UI ─────────────────────────────────────────────
        else
        {
            float panelW = io.DisplaySize.x;
            float panelH = io.DisplaySize.y;

            // ── Top Bar ─────────────────────────────────────────────────
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(panelW, 40));
            ImGui::Begin("##TopBar", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
            {
                std::lock_guard<std::mutex> lk(g_stateMutex);
                ImGui::Text("User: %s", username);
                ImGui::SameLine(200);
                ImGui::Text("Cash: $%d", g_cash);
            }
            ImGui::SameLine(panelW - 120);
            if (ImGui::Button("Disconnect")) {
                SendQuit();
            }
            ImGui::End();

            float contentTop = 40;
            float leftW = panelW * 0.55f;
            float rightW = panelW - leftW;
            float marketH = (panelH - contentTop) * 0.55f;
            float logH = (panelH - contentTop) - marketH;

            // ── Market Data ─────────────────────────────────────────────
            ImGui::SetNextWindowPos(ImVec2(0, contentTop));
            ImGui::SetNextWindowSize(ImVec2(leftW, marketH));
            ImGui::Begin("Market", nullptr,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse);

            if (ImGui::Button("Refresh")) SendMarketRequest();

            if (ImGui::BeginTable("MarketTable", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("Symbol", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("Available", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("Bid", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("Ask", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableHeadersRow();

                std::lock_guard<std::mutex> lk(g_stateMutex);
                for (auto& s : g_market)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("%s", s.symbol.c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%d", s.quantity);
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "$%d", s.bid);
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "$%d", s.ask);
                }
                ImGui::EndTable();
            }
            ImGui::End();

            // ── Activity Log ────────────────────────────────────────────
            ImGui::SetNextWindowPos(ImVec2(0, contentTop + marketH));
            ImGui::SetNextWindowSize(ImVec2(leftW, logH));
            ImGui::Begin("Activity Log", nullptr,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse);
            {
                std::lock_guard<std::mutex> lk(g_stateMutex);
                for (auto& entry : g_log) {
                    ImGui::TextColored(entry.color, "%s", entry.message.c_str());
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
            }
            ImGui::End();

            // ── Right Panel: Portfolio + Trading ────────────────────────
            float rightPanelH = panelH - contentTop;
            ImGui::SetNextWindowPos(ImVec2(leftW, contentTop));
            ImGui::SetNextWindowSize(ImVec2(rightW, rightPanelH));
            ImGui::Begin("Trading", nullptr,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse);

            // ── Portfolio Section ────────────────────────────────────
            if (ImGui::CollapsingHeader("Portfolio",
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::Button("Refresh Portfolio")) SendPortfolioRequest();

                std::lock_guard<std::mutex> lk(g_stateMutex);
                ImGui::Text("Cash: $%d", g_cash);

                if (!g_portfolio.empty() &&
                    ImGui::BeginTable("PortTable", 3,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                {
                    ImGui::TableSetupColumn("Symbol");
                    ImGui::TableSetupColumn("Qty");
                    ImGui::TableSetupColumn("Avg Cost");
                    ImGui::TableHeadersRow();

                    for (auto& h : g_portfolio) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::Text("%s", h.symbol.c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%d", h.quantity);
                        ImGui::TableNextColumn(); ImGui::Text("$%d", h.cost);
                    }
                    ImGui::EndTable();
                }
                else if (g_portfolio.empty()) {
                    ImGui::TextDisabled("(No holdings)");
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ── Buy Section ─────────────────────────────────────────
            if (ImGui::CollapsingHeader("Buy Stocks",
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushID("buy");
                ImGui::InputText("Symbol", buySymbol, sizeof(buySymbol));
                ImGui::InputInt("Quantity", &buyQty);
                if (buyQty < 1) buyQty = 1;

                if (ImGui::Button("Buy", ImVec2(-1, 28))) {
                    if (strlen(buySymbol) > 0) {
                        SendBuy(buySymbol, buyQty);
                    }
                }
                ImGui::PopID();
            }

            ImGui::Spacing();

            // ── Sell Section ────────────────────────────────────────
            if (ImGui::CollapsingHeader("Sell Stocks",
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushID("sell");
                ImGui::InputText("Symbol", sellSymbol, sizeof(sellSymbol));
                ImGui::InputInt("Quantity", &sellQty);
                if (sellQty < 1) sellQty = 1;

                if (ImGui::Button("Sell", ImVec2(-1, 28))) {
                    if (strlen(sellSymbol) > 0) {
                        SendSell(sellSymbol, sellQty);
                    }
                }
                ImGui::PopID();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ── Trade History ───────────────────────────────────────
            if (ImGui::CollapsingHeader("Trade History"))
            {
                if (ImGui::Button("Load History")) SendHistoryRequest();
                ImGui::Spacing();

                std::lock_guard<std::mutex> lk(g_stateMutex);
                if (g_tradeHistory.empty())
                    ImGui::TextDisabled("(No trades yet)");
                else
                    ImGui::TextWrapped("%s", g_tradeHistory.c_str());
            }

            ImGui::End();
        }

        // ── Render ──────────────────────────────────────────────────────
        ImGui::Render();
        const float clear_color[4] = { clearColor.x, clearColor.y,
                                       clearColor.z, clearColor.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // vsync

        if (!g_running) PostMessage(hwnd, WM_CLOSE, 0, 0);
    }

    // Cleanup
    g_running = false;
    if (g_tcpSock != INVALID_SOCKET) {
        shutdown(g_tcpSock, SD_BOTH);
        closesocket(g_tcpSock);
    }
    if (receiverThr.joinable()) receiverThr.join();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    WSACleanup();
    return 0;
}

// ── DirectX 11 Helpers ──────────────────────────────────────────────────────
bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
        &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain)        { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext)  { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)         { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr,
                                         &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_resizeWidth = (UINT)LOWORD(lParam);
        g_resizeHeight = (UINT)HIWORD(lParam);
        g_resizeRequested = true;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
