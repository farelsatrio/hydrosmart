import requests
import json
import websocket
import threading
import time

from flask import Flask, render_template
from flask_socketio import SocketIO

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")  # Izinkan CORS untuk koneksi socket dari browser

# ThingsBoard Configuration
TB_URL = 'http://10.1.0.4:9090'
USERNAME = 'farelsatriop@gmail.com'
PASSWORD = 'farel123'  # Ganti dengan password Anda
DEVICE_ID = 'fdac50b0-35fa-11f0-9af9-29f41711de62'  # Ganti dengan Device ID Anda

# Fungsi untuk mendapatkan JWT token
def get_token():
    response = requests.post(f'{TB_URL}/api/auth/login', json={'username': USERNAME, 'password': PASSWORD})
    if response.status_code == 200:
        return response.json()['token']
    else:
        print('Gagal mendapatkan JWT token')
        return None

token = get_token()
if not token:
    exit("Tidak bisa melanjutkan tanpa token")

# Variabel global untuk menyimpan data terakhir
last_data = {}

# WebSocket ke ThingsBoard
def on_message(ws, message):
    global last_data  # 🔁 Penting! Gunakan global supaya bisa diakses
    try:
        data = json.loads(message)
        if 'data' in data:
            telemetry = data['data']
            if isinstance(telemetry, dict):
                moisture = telemetry.get('moisture', [[None, None]])[-1][1]
                pump_status = telemetry.get('pump_status', [[None, None]])[-1][1]
                auto_mode = telemetry.get('auto_mode', [[None, None]])[-1][1]

                if moisture is not None and pump_status is not None and auto_mode is not None:
                    moisture = int(moisture)
                    pump_status = pump_status.lower() == "true"
                    auto_mode = auto_mode.lower() == "true"

                    current_data = {
                        'moisture': moisture,
                        'pump_status': pump_status,
                        'auto_mode': auto_mode
                    }

                    if current_data != last_data:
                        socketio.emit('update', current_data)
                        last_data = current_data
    except Exception as e:
        print(f"Error memproses pesan: {e}")

def on_error(ws, error):
    print(f"WebSocket error: {error}")

def on_close(ws):
    print("WebSocket ditutup")

def on_open(ws):
    subscription = {
        "tsSubCmds": [{
            "entityType": "DEVICE",
            "entityId": DEVICE_ID,
            "scope": "LATEST_TELEMETRY",
            "cmdId": 1
        }],
        "historyCmds": [],
        "attrSubCmds": []
    }
    ws.send(json.dumps(subscription))

def start_websocket():
    while True:
        try:
            ws_url = f"ws://{TB_URL.replace('http://', '')}/api/ws/plugins/telemetry?token={token}"
            ws = websocket.WebSocketApp(ws_url, on_message=on_message, on_error=on_error, on_close=on_close)
            ws.on_open = on_open
            ws.run_forever()
        except Exception as e:
            print(f"WebSocket error: {e}, reconnecting in 5 seconds...")
        time.sleep(5)

# Jalankan WebSocket di thread terpisah
threading.Thread(target=start_websocket, daemon=True).start()

@app.route('/')
def index():
    return render_template('index.html')

@socketio.on('set_auto_mode')
def handle_set_auto_mode(state):
    print(f"[RPC] Mengirim set_auto_mode: {state}")
    url = f"{TB_URL}/api/rpc/twoway/{DEVICE_ID}"
    headers = {
        'Authorization': f'Bearer {token}',
        'Content-Type': 'application/json'
    }
    payload = {"method": "setState", "params": state}
    try:
        response = requests.post(url, headers=headers, json=payload, timeout=10)
        if response.status_code == 200:
            print("[RPC] Berhasil mengatur auto mode")
        else:
            print(f"[RPC] Gagal: {response.status_code}, {response.text}")
    except requests.exceptions.RequestException as e:
        print(f"[RPC] Error koneksi: {e}")

if __name__ == "__main__":
    socketio.run(app, host='0.0.0.0', port=5000)
