/*!*****************************************************************************
\file   ClientUI.cpp
\brief  ImGui UI rendering for the trading client.

Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*******************************************************************************/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <ws2tcpip.h>

#include "ClientUI.h"

//  Login Screen 
bool RenderLoginScreen(UIState& ui, ImGuiIO& io)
{
    bool connected = false;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f,
                                   io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 220));
    ImGui::Begin("Connect to Server", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Online Trading Platform");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::InputText("Server IP", ui.serverIP, sizeof(ui.serverIP));
    ImGui::InputText("Port", ui.serverPort, sizeof(ui.serverPort));
    ImGui::InputText("Username", ui.username, sizeof(ui.username));

    ImGui::Spacing();
    if (ImGui::Button("Connect & Login", ImVec2(-1, 30)))
    {
        if (strlen(ui.username) > 0)
        {
            g_tcpSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in srvAddr{};
            srvAddr.sin_family = AF_INET;
            srvAddr.sin_port = htons((u_short)atoi(ui.serverPort));
            inet_pton(AF_INET, ui.serverIP, &srvAddr.sin_addr);

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
                SendLogin(ui.username);
                connected = true;
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
    return connected;
}

//  Main Trading UI 
void RenderTradingUI(UIState& ui, ImGuiIO& io)
{
    float panelW = io.DisplaySize.x;
    float panelH = io.DisplaySize.y;

    //  Top Bar 
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(panelW, 40));
    ImGui::Begin("##TopBar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    {
        std::lock_guard<std::mutex> lk(g_stateMutex);
        ImGui::Text("User: %s", ui.username);
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

    //  Market Data 
    ImGui::SetNextWindowPos(ImVec2(0, contentTop));
    ImGui::SetNextWindowSize(ImVec2(leftW, marketH));
    ImGui::Begin("Market", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse);

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

    //  Price Trend Graphs 
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("Ctrl+Scroll to zoom | Drag to pan | Double-click to reset");

    {
        std::lock_guard<std::mutex> lk(g_stateMutex);
        double now = std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        for (auto& s : g_market)
        {
            auto phIt = g_priceHistory.find(s.symbol);
            if (phIt == g_priceHistory.end() ||
                phIt->second.bidHistory.size() < 2)
                continue;

            if (ImGui::TreeNode(s.symbol.c_str()))
            {
                auto& fullPh = phIt->second;
                int totalPts = (int)fullPh.timestamps.size();
                double dataStart = fullPh.timestamps.front();

                // Get or create view state for this stock
                auto& gv = g_graphViews[s.symbol];

                // Compute visible time window
                double viewRight = now + gv.viewEnd;
                double viewLeft  = viewRight - gv.viewDuration;

                // Clamp: don't scroll past the earliest data
                if (viewLeft < dataStart) {
                    viewLeft = dataStart;
                    viewRight = viewLeft + gv.viewDuration;
                }

                // Find data range indices within the view
                int startIdx = totalPts;
                for (int i = 0; i < totalPts; ++i) {
                    if (fullPh.timestamps[i] >= viewLeft) {
                        startIdx = i;
                        break;
                    }
                }
                if (startIdx > 0) startIdx--;

                int endIdx = totalPts - 1;
                for (int i = totalPts - 1; i >= 0; --i) {
                    if (fullPh.timestamps[i] <= viewRight) {
                        endIdx = i;
                        break;
                    }
                }
                if (endIdx < totalPts - 1) endIdx++;

                int count = endIdx - startIdx + 1;
                if (count < 2) {
                    ImGui::TextDisabled("Not enough data");
                    ImGui::TreePop();
                    continue;
                }

                const float* bidData = fullPh.bidHistory.data() + startIdx;
                const float* askData = fullPh.askHistory.data() + startIdx;
                const double* tsData = fullPh.timestamps.data() + startIdx;

                // Y-axis scale from visible data
                float yMin = FLT_MAX, yMax = -FLT_MAX;
                for (int i = 0; i < count; ++i) {
                    if (bidData[i] < yMin) yMin = bidData[i];
                    if (askData[i] > yMax) yMax = askData[i];
                }
                float pad = (yMax - yMin) * 0.1f;
                if (pad < 1.0f) pad = 1.0f;
                yMin -= pad;
                yMax += pad;
                float yRange = yMax - yMin;
                if (yRange < 1.0f) yRange = 1.0f;

                ImU32 colBid      = IM_COL32(77, 200, 255, 255);
                ImU32 colAsk      = IM_COL32(255, 180, 77, 255);
                ImU32 colBidUp    = IM_COL32(77, 255, 77, 255);
                ImU32 colBidDown  = IM_COL32(255, 77, 77, 255);
                ImU32 colAskUp    = IM_COL32(77, 255, 77, 200);
                ImU32 colAskDown  = IM_COL32(255, 77, 77, 200);
                ImU32 colBg       = IM_COL32(30, 30, 40, 255);
                ImU32 colGrid     = IM_COL32(60, 60, 80, 255);
                ImU32 colText     = IM_COL32(120, 120, 150, 255);

                float graphH = 100.0f;
                float timeAxisH = 16.0f;
                float totalH = graphH + timeAxisH;

                // Legend
                char legendBuf[64];
                snprintf(legendBuf, sizeof(legendBuf),
                    "Bid $%d  |  Ask $%d", s.bid, s.ask);
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1),
                    "%s", legendBuf);
                if (gv.viewEnd < -0.5) {
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
                    ImGui::TextColored(ImVec4(1, 1, 0.3f, 1),
                        "[HISTORY]");
                }

                ImVec2 canvasPos = ImGui::GetCursorScreenPos();
                float canvasW = ImGui::GetContentRegionAvail().x;

                ImGui::PushID(s.symbol.c_str());
                ImGui::InvisibleButton("##graph",
                    ImVec2(canvasW, totalH));
                bool hovered = ImGui::IsItemHovered();
                bool active  = ImGui::IsItemActive();

                // Mouse wheel zoom
                if (hovered && io.KeyCtrl) {
                    float wheel = io.MouseWheel;
                    if (wheel != 0.0f) {
                        double zoomFactor = (wheel > 0) ? 0.8 : 1.25;
                        double newDur = gv.viewDuration * zoomFactor;
                        double totalRange = now - dataStart;
                        if (totalRange < 10.0) totalRange = 10.0;
                        if (newDur < 5.0) newDur = 5.0;
                        if (newDur > totalRange) newDur = totalRange;

                        float mouseRatio = (io.MousePos.x - canvasPos.x)
                            / canvasW;
                        double mouseTime = viewLeft
                            + (viewRight - viewLeft) * mouseRatio;
                        double newLeft = mouseTime - newDur * mouseRatio;
                        double newRight = newLeft + newDur;

                        gv.viewDuration = newDur;
                        gv.viewEnd = newRight - now;
                        if (gv.viewEnd > 0) gv.viewEnd = 0;
                    }
                }

                // Drag to pan
                if (active && ImGui::IsMouseDragging(
                        ImGuiMouseButton_Left)) {
                    if (!gv.dragging) {
                        gv.dragging = true;
                        gv.dragStartX = io.MousePos.x;
                        gv.dragStartEnd = gv.viewEnd;
                    }
                    float dx = io.MousePos.x - gv.dragStartX;
                    double timeDelta = -(dx / canvasW) * gv.viewDuration;
                    gv.viewEnd = gv.dragStartEnd + timeDelta;
                    if (gv.viewEnd > 0) gv.viewEnd = 0;
                } else {
                    gv.dragging = false;
                }

                // Double-click to reset
                if (hovered && ImGui::IsMouseDoubleClicked(
                        ImGuiMouseButton_Left)) {
                    gv.viewEnd = 0;
                    gv.viewDuration = 60.0;
                }

                ImGui::PopID();

                ImDrawList* dl = ImGui::GetWindowDrawList();

                // Background
                dl->AddRectFilled(canvasPos,
                    ImVec2(canvasPos.x + canvasW,
                           canvasPos.y + graphH), colBg);

                // Horizontal grid lines
                for (int g = 1; g <= 3; ++g) {
                    float gy = canvasPos.y + graphH * g / 4.0f;
                    dl->AddLine(ImVec2(canvasPos.x, gy),
                        ImVec2(canvasPos.x + canvasW, gy), colGrid);
                }

                auto timeToX = [&](double t) -> float {
                    return canvasPos.x + (float)((t - viewLeft)
                        / gv.viewDuration) * canvasW;
                };
                auto priceToY = [&](float price) -> float {
                    return canvasPos.y + graphH
                        - ((price - yMin) / yRange) * graphH;
                };

                dl->PushClipRect(canvasPos,
                    ImVec2(canvasPos.x + canvasW,
                           canvasPos.y + graphH), true);

                // Draw bid line
                for (int i = 0; i < count - 1; ++i) {
                    float x0 = timeToX(tsData[i]);
                    float x1 = timeToX(tsData[i + 1]);
                    float y0 = priceToY(bidData[i]);
                    float y1 = priceToY(bidData[i + 1]);
                    ImU32 col = (bidData[i + 1] >= bidData[i])
                        ? colBidUp : colBidDown;
                    dl->AddLine(ImVec2(x0, y0),
                        ImVec2(x1, y1), col, 2.0f);
                }

                // Draw ask line
                for (int i = 0; i < count - 1; ++i) {
                    float x0 = timeToX(tsData[i]);
                    float x1 = timeToX(tsData[i + 1]);
                    float y0 = priceToY(askData[i]);
                    float y1 = priceToY(askData[i + 1]);
                    ImU32 col = (askData[i + 1] >= askData[i])
                        ? colAskUp : colAskDown;
                    dl->AddLine(ImVec2(x0, y0),
                        ImVec2(x1, y1), col, 1.5f);
                }

                // Shade the spread
                for (int i = 0; i < count - 1; ++i) {
                    float x0 = timeToX(tsData[i]);
                    float x1 = timeToX(tsData[i + 1]);
                    dl->AddQuadFilled(
                        ImVec2(x0, priceToY(bidData[i])),
                        ImVec2(x1, priceToY(bidData[i + 1])),
                        ImVec2(x1, priceToY(askData[i + 1])),
                        ImVec2(x0, priceToY(askData[i])),
                        IM_COL32(100, 100, 200, 30));
                }

                dl->PopClipRect();

                // Price labels
                char minBuf[16], maxBuf[16];
                snprintf(maxBuf, sizeof(maxBuf), "$%.0f", yMax - pad);
                snprintf(minBuf, sizeof(minBuf), "$%.0f", yMin + pad);
                dl->AddText(ImVec2(canvasPos.x + 4,
                    canvasPos.y + 2), colText, maxBuf);
                dl->AddText(ImVec2(canvasPos.x + 4,
                    canvasPos.y + graphH - 14), colText, minBuf);

                // Current price labels
                float bidY = priceToY((float)s.bid);
                float askY = priceToY((float)s.ask);
                char bidPriceBuf[16], askPriceBuf[16];
                snprintf(bidPriceBuf, sizeof(bidPriceBuf),
                    "B$%d", s.bid);
                snprintf(askPriceBuf, sizeof(askPriceBuf),
                    "A$%d", s.ask);
                dl->AddText(ImVec2(canvasPos.x + canvasW - 40,
                    bidY - 6), colBid, bidPriceBuf);
                dl->AddText(ImVec2(canvasPos.x + canvasW - 40,
                    askY - 6), colAsk, askPriceBuf);

                // Time axis labels
                float labelY = canvasPos.y + graphH + 2.0f;
                int numLabels = (canvasW > 400) ? 5 : 3;
                for (int l = 0; l <= numLabels; ++l) {
                    double t = viewLeft
                        + gv.viewDuration * l / (double)numLabels;
                    float lx = timeToX(t);

                    dl->AddLine(
                        ImVec2(lx, canvasPos.y + graphH),
                        ImVec2(lx, canvasPos.y + graphH + 4),
                        colGrid);

                    std::time_t tt = (std::time_t)t;
                    struct tm tmBuf;
                    localtime_s(&tmBuf, &tt);
                    char timeBuf[32];
                    if (gv.viewDuration <= 300.0)
                        strftime(timeBuf, sizeof(timeBuf),
                            "%H:%M:%S", &tmBuf);
                    else if (gv.viewDuration <= 7200.0)
                        strftime(timeBuf, sizeof(timeBuf),
                            "%H:%M", &tmBuf);
                    else
                        strftime(timeBuf, sizeof(timeBuf),
                            "%m/%d %H:%M", &tmBuf);
                    dl->AddText(ImVec2(lx - 20, labelY),
                        colText, timeBuf);
                }

                ImGui::TreePop();
            }
        }
    }

    ImGui::End();

    //  Activity Log 
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

    //  Right Panel: Portfolio + Trading 
    float rightPanelH = panelH - contentTop;
    ImGui::SetNextWindowPos(ImVec2(leftW, contentTop));
    ImGui::SetNextWindowSize(ImVec2(rightW, rightPanelH));
    ImGui::Begin("Trading", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse);

    //  Portfolio Section 
    if (ImGui::CollapsingHeader("Portfolio",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
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

    //  Buy Section 
    if (ImGui::CollapsingHeader("Buy Stocks",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("buy");
        ImGui::InputText("Symbol", ui.buySymbol, sizeof(ui.buySymbol));
        ImGui::InputInt("Quantity", &ui.buyQty);
        if (ui.buyQty < 1) ui.buyQty = 1;

        if (ImGui::Button("Buy", ImVec2(-1, 28))) {
            if (strlen(ui.buySymbol) > 0) {
                SendBuy(ui.buySymbol, ui.buyQty);
            }
        }
        ImGui::PopID();
    }

    ImGui::Spacing();

    //  Sell Section 
    if (ImGui::CollapsingHeader("Sell Stocks",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("sell");
        ImGui::InputText("Symbol", ui.sellSymbol, sizeof(ui.sellSymbol));
        ImGui::InputInt("Quantity", &ui.sellQty);
        if (ui.sellQty < 1) ui.sellQty = 1;

        if (ImGui::Button("Sell", ImVec2(-1, 28))) {
            if (strlen(ui.sellSymbol) > 0) {
                SendSell(ui.sellSymbol, ui.sellQty);
            }
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    //  Trade History 
    if (ImGui::CollapsingHeader("Trade History",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        float remaining = ImGui::GetContentRegionAvail().y;
        if (remaining < 60.0f) remaining = 60.0f;
        ImGui::BeginChild("TradeHistoryScroll", ImVec2(0, remaining), true);
        {
            std::lock_guard<std::mutex> lk(g_stateMutex);
            if (g_tradeHistory.empty())
                ImGui::TextDisabled("(No trades yet)");
            else
                ImGui::TextWrapped("%s", g_tradeHistory.c_str());

            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }

    ImGui::End();
}
