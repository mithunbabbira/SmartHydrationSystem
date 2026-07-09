# Onocoy Station Monitor

<img width="1710" height="1135" alt="Screenshot 2026-03-20 at 4 34 37 PM" src="https://github.com/user-attachments/assets/3cb4c159-5382-4a0f-b494-56645afdc754" />

## Running on a Raspberry Pi

### Prerequisites
Make sure you have Python 3 installed. You can install the required packages using:
```bash
pip install -r requirements.txt --break-system-packages
```

### Starting the Server
Since this is a FastAPI web application, it needs an ASGI server to serve requests. 

**Method 1: Run via Python (Recommended)**
Simply execute the main script. We've configured it to automatically start the server:
```bash
python main.py
```

**Method 2: Run via Uvicorn**
Alternatively, you can start the application using the `uvicorn` command directly:
```bash
uvicorn main:app --host 0.0.0.0 --port 8000
```

### Accessing the Dashboard
Once the server is running, it will listen on all network interfaces (`0.0.0.0`). You can access the dashboard by navigating to the Raspberry Pi's IP address from any device on your local network:
`http://<RASPBERRY_PI_IP>:8000`

If you are opening it directly on the Raspberry Pi's desktop environment, you can use:
`http://localhost:8000`
