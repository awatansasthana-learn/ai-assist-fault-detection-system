from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from datetime import datetime

app = FastAPI(title="AI Assist Fault Detection System")


# =========================
# CORS
# =========================

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# =========================
# SENSOR DATA
# =========================

class SensorData(BaseModel):
    temperature: float
    humidity: float
    current: float
    status: str
    servo: int


# =========================
# SERVO CONTROL
# =========================

class ServoCommand(BaseModel):
    mode: str


servo_command = {
    "mode": "OPEN"
}


# =========================
# LATEST DATA
# =========================

latest_data = {
    "temperature": 0,
    "humidity": 0,
    "current": 0,
    "status": "WAITING",
    "servo": 90,
    "timestamp": None
}


# =========================
# HOME
# =========================

@app.get("/")
def home():
    return {
        "message": "AI Assist Fault Detection Backend is running"
    }


# =========================
# ESP32 SENDS SENSOR DATA
# =========================

@app.post("/api/data")
def receive_data(data: SensorData):

    global latest_data

    latest_data = {
        "temperature": data.temperature,
        "humidity": data.humidity,
        "current": data.current,
        "status": data.status,
        "servo": data.servo,
        "timestamp": datetime.now().isoformat()
    }

    print(
        f"Temperature: {data.temperature} °C | "
        f"Humidity: {data.humidity} % | "
        f"Current: {data.current} A | "
        f"Status: {data.status} | "
        f"Servo: {data.servo}°"
    )

    return {
        "success": True,
        "message": "Data received"
    }


# =========================
# FRONTEND GETS SENSOR DATA
# =========================

@app.get("/api/data")
def get_data():
    return latest_data


# =========================
# FRONTEND SENDS SERVO COMMAND
# =========================

@app.post("/api/control")
def control_servo(command: ServoCommand):

    global servo_command

    mode = command.mode.upper()

    if mode not in ["OPEN", "CLOSE", "AUTO"]:
        return {
            "success": False,
            "message": "Invalid mode"
        }

    servo_command["mode"] = mode

    print(
        f"SERVO COMMAND RECEIVED: {mode}"
    )

    return {
        "success": True,
        "mode": mode
    }


# =========================
# ESP32 GETS SERVO COMMAND
# =========================

@app.get("/api/control")
def get_control():

    return servo_command