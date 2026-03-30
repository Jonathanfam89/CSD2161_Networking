"""
Mock trading script - connects multiple bot users and performs random
buy/sell trades to generate interesting price movement graphs.

Usage:
  python mock_trades.py [host] [port]
  Defaults: 127.0.0.1 8888
"""

import socket
import struct
import time
import random
import sys
import threading

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8888

# Protocol command IDs
REQ_LOGIN      = 0x02
RSP_LOGIN      = 0x03
REQ_BUY        = 0x04
RSP_BUY        = 0x05
REQ_SELL       = 0x06
RSP_SELL       = 0x07
RSP_MARKET     = 0x12
RSP_MARKETUPDATE = 0x13
RSP_ERROR      = 0x25
REQ_QUIT       = 0x01

STOCKS = ["AAPL", "MSFT", "GOOG", "AMZN", "TSLA", "NVDA", "META", "NFLX"]

def recvall(sock, n):
    data = b""
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError("Connection closed")
        data += chunk
    return data

def send_login(sock, username):
    name_bytes = username.encode()
    sock.sendall(struct.pack("!B", REQ_LOGIN))
    sock.sendall(struct.pack("!I", len(name_bytes)))
    sock.sendall(name_bytes)

def recv_login(sock):
    cmd = struct.unpack("!B", recvall(sock, 1))[0]
    if cmd == RSP_LOGIN:
        balance = struct.unpack("!I", recvall(sock, 4))[0]
        return balance
    return None

def recv_market(sock):
    """Read a market response (RSP_MARKET or RSP_MARKETUPDATE)."""
    count = struct.unpack("!I", recvall(sock, 4))[0]
    stocks = {}
    for _ in range(count):
        sym = recvall(sock, 8).rstrip(b"\x00").decode()
        qty, bid, ask = struct.unpack("!III", recvall(sock, 12))
        stocks[sym] = (qty, bid, ask)
    return stocks

def send_buy(sock, symbol, qty):
    sock.sendall(struct.pack("!B", REQ_BUY))
    sym_buf = symbol.encode().ljust(8, b"\x00")[:8]
    sock.sendall(sym_buf)
    sock.sendall(struct.pack("!I", qty))

def send_sell(sock, symbol, qty):
    sock.sendall(struct.pack("!B", REQ_SELL))
    sym_buf = symbol.encode().ljust(8, b"\x00")[:8]
    sock.sendall(sym_buf)
    sock.sendall(struct.pack("!I", qty))

def drain_response(sock):
    """Read one server response and discard it."""
    cmd = struct.unpack("!B", recvall(sock, 1))[0]
    if cmd == RSP_BUY or cmd == RSP_SELL:
        recvall(sock, 8 + 4 + 4 + 4)  # symbol + qty + price + cash
    elif cmd == RSP_MARKET or cmd == RSP_MARKETUPDATE:
        recv_market(sock)
    elif cmd == RSP_ERROR:
        msg_len = struct.unpack("!I", recvall(sock, 4))[0]
        recvall(sock, msg_len)
    elif cmd == RSP_LOGIN:
        recvall(sock, 4)

def drain_all(sock, timeout=0.3):
    """Drain any pending responses with a short timeout."""
    sock.settimeout(timeout)
    try:
        while True:
            drain_response(sock)
    except (socket.timeout, ConnectionError, struct.error):
        pass
    sock.settimeout(None)


class BotTrader:
    def __init__(self, username):
        self.username = username
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((HOST, PORT))
        self.holdings = {}  # symbol -> qty owned

        # Login
        send_login(self.sock, username)
        self.balance = recv_login(self.sock)
        # Drain the initial RSP_MARKET
        drain_all(self.sock)
        print(f"  [{username}] Connected. Balance: ${self.balance}")

    def buy(self, symbol, qty):
        send_buy(self.sock, symbol, qty)
        drain_all(self.sock)
        self.holdings[symbol] = self.holdings.get(symbol, 0) + qty
        print(f"  [{self.username}] BUY  {qty} x {symbol}")

    def sell(self, symbol, qty):
        send_sell(self.sock, symbol, qty)
        drain_all(self.sock)
        self.holdings[symbol] = self.holdings.get(symbol, 0) - qty
        if self.holdings[symbol] <= 0:
            del self.holdings[symbol]
        print(f"  [{self.username}] SELL {qty} x {symbol}")

    def disconnect(self):
        try:
            self.sock.sendall(struct.pack("!B", REQ_QUIT))
            self.sock.close()
        except Exception:
            pass
        print(f"  [{self.username}] Disconnected.")


DURATION = 300  # 5 minutes

stop_event = threading.Event()


def bot_thread(bot):
    """Each bot trades independently in its own thread."""
    while not stop_event.is_set():
        symbol = random.choice(STOCKS)
        qty = random.randint(1, 4)

        try:
            if random.random() < 0.55:  # slight buy bias
                bot.buy(symbol, qty)
            else:
                held = bot.holdings.get(symbol, 0)
                if held >= qty:
                    bot.sell(symbol, qty)
                else:
                    bot.buy(symbol, qty)
        except (ConnectionError, OSError):
            print(f"  [{bot.username}] Connection lost.")
            break

        time.sleep(random.uniform(0.3, 1.0))


def run_simulation():
    print(f"\nConnecting bots to {HOST}:{PORT}...")
    print("=" * 50)

    bots = []
    bot_names = ["bot_alice", "bot_bob", "bot_charlie", "bot_dave", "bot_eve"]

    for name in bot_names:
        try:
            bots.append(BotTrader(name))
            time.sleep(0.2)
        except ConnectionError:
            print(f"  Failed to connect {name}")

    if not bots:
        print("No bots connected. Is the server running?")
        return

    print(f"\n{'=' * 50}")
    print(f"Starting mock trades for {DURATION}s ({DURATION // 60}m)... (Ctrl+C to stop)")
    print(f"{'=' * 50}\n")

    threads = []
    for bot in bots:
        t = threading.Thread(target=bot_thread, args=(bot,), daemon=True)
        t.start()
        threads.append(t)

    try:
        start = time.time()
        while time.time() - start < DURATION:
            elapsed = int(time.time() - start)
            remaining = DURATION - elapsed
            mins, secs = divmod(remaining, 60)
            print(f"\r  Time remaining: {mins}m {secs:02d}s", end="", flush=True)
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n\nStopping early...")

    stop_event.set()
    for t in threads:
        t.join(timeout=3)

    print(f"\n\n{'=' * 50}")
    print("Simulation complete! Check the client graphs.")
    print(f"{'=' * 50}\n")

    # Disconnect bots
    for bot in bots:
        bot.disconnect()
        time.sleep(0.1)


if __name__ == "__main__":
    run_simulation()
