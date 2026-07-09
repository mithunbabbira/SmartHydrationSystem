import asyncio
import logging
import httpx
import json
import os
from datetime import datetime, timezone
from fastapi import FastAPI, HTTPException, Form
from fastapi.responses import HTMLResponse, RedirectResponse
from pydantic import BaseModel
from contextlib import asynccontextmanager

# Configure logging for easier debugging in development/production
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

STATIONS_FILE = "stations.json"
SETTINGS_FILE = "settings.json"

def load_settings() -> dict:
    if os.path.exists(SETTINGS_FILE):
        try:
            with open(SETTINGS_FILE, "r") as f:
                return json.load(f)
        except Exception as e:
            logger.error(f"Error loading {SETTINGS_FILE}: {e}")
    
    # Default configs
    default = {
        "polling_interval": int(os.getenv("POLLING_INTERVAL", 60))
    }
    save_settings(default)
    return default

def save_settings(data: dict):
    try:
        with open(SETTINGS_FILE, "w") as f:
            json.dump(data, f, indent=4)
        logger.info(f"Settings saved to {SETTINGS_FILE} successfully.")
    except Exception as e:
        logger.error(f"Error saving {SETTINGS_FILE}: {e}")

def load_stations() -> dict:
    if os.path.exists(STATIONS_FILE):
        try:
            with open(STATIONS_FILE, "r") as f:
                return json.load(f)
        except Exception as e:
            logger.error(f"Error loading {STATIONS_FILE}: {e}")
    
    # Default if file does not exist - empty so no user data is hardcoded for public repos
    default = {}
    save_stations(default)
    return default

def save_stations(data: dict):
    try:
        with open(STATIONS_FILE, "w") as f:
            json.dump(data, f, indent=4)
        logger.info(f"Stations saved to {STATIONS_FILE} successfully.")
    except Exception as e:
        logger.error(f"Error saving {STATIONS_FILE}: {e}")

# Load initial dynamic registry and settings
STATIONS = load_stations()
SETTINGS = load_settings()

API_BASE_URL = "https://api.onocoy.com/api/v1/explorer/server/{station_id}/info"

async def poll_station_data():
    """Background task to fetch latest station status from Onocoy."""
    async with httpx.AsyncClient() as client:
        while True:
            # Check current polling interval from settings
            current_interval = SETTINGS.get("polling_interval", 60)
            
            # Cast to list so we can modify the original dictionary via forms safely without throwing a RuntimeError constraint
            station_ids = list(STATIONS.keys())
            
            for station_id in station_ids:
                url = API_BASE_URL.format(station_id=station_id)
                try:
                    # Timeout is needed so one stalled connection doesn't stop polling
                    response = await client.get(url, timeout=10.0)
                    now_iso = datetime.now(timezone.utc).isoformat()
                    
                    if response.status_code == 200:
                        data = response.json()
                        raw_status = data.get("status", {})
                        
                        is_up = raw_status.get("is_up", False)
                        since = raw_status.get("since", None)
                        
                        # Apply updates if it hasn't been deleted out from under us
                        if station_id in STATIONS:
                            STATIONS[station_id]["status"] = "Online" if is_up else "Offline"
                            STATIONS[station_id]["last_updated"] = since
                            STATIONS[station_id]["last_checked"] = now_iso
                            logger.info(f"Polled {station_id}: => {STATIONS[station_id]['status']}")
                    else:
                        logger.warning(f"Failed to fetch {station_id}. Status Code: {response.status_code}")
                        if station_id in STATIONS:
                            STATIONS[station_id]["last_checked"] = now_iso
                except Exception as e:
                    logger.error(f"Error fetching data for {station_id}: {e}")
                    if station_id in STATIONS:
                        STATIONS[station_id]["last_checked"] = datetime.now(timezone.utc).isoformat()
            
            # Save any new statuses back to JSON so last_updated isn't lost on restart
            save_stations(STATIONS)
            
            # Wait for the next polling cycle based on dynamic interval
            await asyncio.sleep(current_interval)

@asynccontextmanager
async def lifespan(app: FastAPI):
    # Startup: Create the background polling task
    logger.info("Application starting up... Beginning background data polling.")
    polling_task = asyncio.create_task(poll_station_data())
    yield
    # Shutdown: Cancel the task
    logger.info("Application shutting down...")
    polling_task.cancel()

app = FastAPI(title="Onocoy Station Status API", lifespan=lifespan)

def generate_dashboard_html(current_interval: int) -> str:
    """
    Generates the static HTML dashboard template.
    Uses Javascript to asynchronously fetch the API state instead of reloading standard HTML.
    """
    html_content = f"""
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Onocoy Status Dashboard</title>
        <style>
            body {{ font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 40px; background-color: #f9f9f9;}}
            h1 {{ color: #333; }}
            table {{ border-collapse: collapse; width: 100%; max-width: 900px; background-color: #fff; box-shadow: 0 4px 6px rgba(0,0,0,0.1); margin-bottom: 40px;}}
            th, td {{ border: 1px solid #ddd; padding: 12px; text-align: left; }}
            th {{ background-color: #f2f2f2; font-weight: 600; }}
            tr:hover {{ background-color: #f5f5f5; }}
            .online {{ color: #28a745; font-weight: bold; }}
            .offline {{ color: #dc3545; font-weight: bold; }}
            
            .manage-forms {{ display: flex; flex-wrap: wrap; gap: 20px; }}
            .form-card {{ background: #fff; padding: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); width: 280px; border-radius: 8px;}}
            .form-card h3 {{ margin-top: 0; }}
            .form-card input {{ width: 90%; margin-bottom: 15px; padding: 8px; border: 1px solid #ccc; border-radius: 4px; }}
            .form-card button {{ padding: 10px 15px; color: white; border: none; border-radius: 4px; cursor: pointer; font-weight: bold;}}
            .btn-blue {{ background: #007bff; }}
            .btn-blue:hover {{ background: #0056b3; }}
            .btn-red {{ background: #dc3545; }}
            .btn-red:hover {{ background: #b02a37; }}
            .btn-green {{ background: #28a745; }}
            .btn-green:hover {{ background: #218838; }}
        </style>
    </head>
    <body>
        <h1>Onocoy Station Status Dashboard</h1>
        <h3>(Live Polling from Onocoy API)</h3>
        <table>
            <thead>
                <tr>
                    <th>Station ID (Mountpoint)</th>
                    <th>Nickname</th>
                    <th>Status</th>
                    <th>Status Since (Onocoy)</th>
                    <th>Last Checked (Local Time)</th>
                </tr>
            </thead>
            <tbody id="station-data">
                <tr><td colspan="5">Loading real-time data...</td></tr>
            </tbody>
        </table>
        
        <hr style="margin-top: 40px; margin-bottom: 20px; border: 1px solid #ddd; max-width: 900px; margin-left: 0;">
        <h2>Manage Stations & Settings</h2>
        <div class="manage-forms">
            <!-- Add/Edit form -->
            <div class="form-card">
                <h3>➕ Add / Edit Station</h3>
                <form id="add-form" action="/manage-station" method="post">
                    <input type="hidden" name="action" value="add">
                    <label>Station ID:</label><br>
                    <input type="text" name="station_id" required placeholder="e.g. STATION_A1"><br>
                    <label>Nickname:</label><br>
                    <input type="text" name="nickname" required placeholder="e.g. My Station Name"><br>
                    <button type="submit" class="btn-blue">Save Station</button>
                </form>
            </div>

            <!-- Remove form -->
            <div class="form-card">
                <h3>🗑️ Remove Station</h3>
                <form id="remove-form" action="/manage-station" method="post">
                    <input type="hidden" name="action" value="remove">
                    <label>Station ID:</label><br>
                    <input type="text" name="station_id" required placeholder="e.g. STATION_A1"><br>
                    <button type="submit" class="btn-red">Remove Station</button>
                </form>
            </div>
            
            <!-- Settings form -->
            <div class="form-card">
                <h3>⚙️ App Settings</h3>
                <form action="/manage-settings" method="post">
                    <label>Polling Interval (seconds):</label><br>
                    <input type="number" name="polling_interval" required min="5" value="{current_interval}"><br>
                    <button type="submit" class="btn-green">Save Settings</button>
                </form>
            </div>
        </div>

        <script>
            function formatDate(isoString) {{
                if (!isoString) return 'Never';
                try {{
                    const d = new Date(isoString);
                    if (isNaN(d.getTime())) return isoString; // fallback
                    return d.toLocaleString(); // converts to local timezone string nicely!
                }} catch (e) {{
                    return isoString;
                }}
            }}

            async function fetchStations() {{
                try {{
                    const response = await fetch('/status');
                    const data = await response.json();
                    const tbody = document.getElementById('station-data');
                    tbody.innerHTML = '';
                    
                    if (Object.keys(data).length === 0) {{
                        tbody.innerHTML = '<tr><td colspan="5">No stations added yet. Manage them below!</td></tr>';
                        return;
                    }}

                    for (const [station_id, info] of Object.entries(data)) {{
                        const tr = document.createElement('tr');
                        const statusClass = info.status === 'Online' ? 'online' : 'offline';
                        
                        // Parse timestamps using JS
                        let last_updated = formatDate(info.last_updated);
                        let last_checked = formatDate(info.last_checked);
                        
                        tr.innerHTML = `
                            <td>${{station_id}}</td>
                            <td>${{info.nickname}}</td>
                            <td class="${{statusClass}}">${{info.status}}</td>
                            <td>${{last_updated}}</td>
                            <td>${{last_checked}}</td>
                        `;
                        tbody.appendChild(tr);
                    }}
                }} catch (error) {{
                    console.error('Error fetching station statuses:', error);
                }}
            }}

            // Initial fetch on load
            fetchStations();

            // Then automatically fetch every 3 seconds to ensure UI updates without user refreshing 
            // (Note: This is just UI refreshing. The backend still polls Onocoy based on your actual polling interval!)
            setInterval(fetchStations, 3000);

            // Optional: Intercept the forms so the page literally never reloads
            document.getElementById('add-form').addEventListener('submit', async (e) => {{
                e.preventDefault();
                await fetch('/manage-station', {{
                    method: 'POST',
                    body: new FormData(e.target)
                }});
                e.target.reset(); // clear inputs
                fetchStations();  // immediately refresh table!
            }});

            document.getElementById('remove-form').addEventListener('submit', async (e) => {{
                e.preventDefault();
                await fetch('/manage-station', {{
                    method: 'POST',
                    body: new FormData(e.target)
                }});
                e.target.reset(); // clear inputs
                fetchStations();  // immediately refresh table!
            }});
        </script>
    </body>
    </html>
    """
    return html_content

@app.post("/manage-station")
def manage_station(action: str = Form(...), station_id: str = Form(...), nickname: str = Form(None)):
    """
    Dynamic endpoint to handle adding or removing stations seamlessly.
    """
    station_id = station_id.strip()
    
    if action == "add":
        if not nickname:
            nickname = station_id
            
        # Keep existing stat if merely editing the nickname
        status = "Offline"
        last_updated = None
        last_checked = None
        if station_id in STATIONS:
            status = STATIONS[station_id].get("status", "Offline")
            last_updated = STATIONS[station_id].get("last_updated", None)
            last_checked = STATIONS[station_id].get("last_checked", None)
            
        STATIONS[station_id] = {
            "nickname": nickname.strip(),
            "status": status,
            "last_updated": last_updated,
            "last_checked": last_checked
        }
        logger.info(f"Station dynamically added/edited: {station_id}")
        save_stations(STATIONS)
        
    elif action == "remove":
        if station_id in STATIONS:
            del STATIONS[station_id]
            logger.info(f"Station dynamically removed: {station_id}")
            save_stations(STATIONS)
            
    # Return 200 OK since the frontend is now using Javascript to intercept forms directly!
    return {"message": "Success"}

@app.post("/manage-settings")
def manage_settings(polling_interval: int = Form(...)):
    """
    Endpoint to dynamically update global app settings.
    """
    if polling_interval < 5:
        polling_interval = 5  # Enforce a minimum polling interval to avoid spam
        
    SETTINGS["polling_interval"] = polling_interval
    save_settings(SETTINGS)
    logger.info(f"Settings updated dynamically: Polling Interval = {polling_interval}s")
    
    return RedirectResponse(url="/", status_code=303)

@app.get("/status")
def get_status():
    """
    Returns the clean JSON summary of all stations and their statuses.
    """
    return STATIONS

@app.get("/", response_class=HTMLResponse)
def dashboard():
    """
    Returns the basic HTML dashboard with color-coded glanceable status.
    Auto-refreshes every 3 seconds via asynchronous Javascript running in the client.
    """
    try:
        html = generate_dashboard_html(SETTINGS.get("polling_interval", 60))
        return html
    except Exception as e:
        logger.error(f"Error generating dashboard UI: {e}")
        return HTMLResponse(content="<h1>Server Error generating dashboard</h1>", status_code=500)

if __name__ == "__main__":
    import uvicorn
    # Run the application on all interfaces (0.0.0.0) so it's accessible from other network devices
    uvicorn.run("main:app", host="0.0.0.0", port=8000)

