#include "web_interface.h"
#include <Arduino.h>
#include <algorithm>
#include <array>
#include <set>
#include <stdlib.h>
#include <cstring>
#include <vector>
#include <unordered_map>

namespace
{
    constexpr uint32_t CHANGE_EXPIRATION_MS = 10000;

    struct ChangeRecord
    {
        uint32_t timestamp = 0;
        uint8_t byteIndex = 0;
        uint8_t oldValue = 0;
        uint8_t newValue = 0;
    };

    std::unordered_map<uint32_t, std::vector<ChangeRecord>> s_changeHistory;

    void pruneHistoryForId(uint32_t id, uint32_t now)
    {
        auto it = s_changeHistory.find(id);
        if (it == s_changeHistory.end())
        {
            return;
        }

        auto &history = it->second;
        history.erase(std::remove_if(history.begin(), history.end(), [now](const ChangeRecord &record)
        {
            return now - record.timestamp > CHANGE_EXPIRATION_MS;
        }), history.end());

        if (history.empty())
        {
            s_changeHistory.erase(it);
        }
    }

    void pruneAllHistory(uint32_t now)
    {
        for (auto it = s_changeHistory.begin(); it != s_changeHistory.end(); )
        {
            auto &history = it->second;
            history.erase(std::remove_if(history.begin(), history.end(), [now](const ChangeRecord &record)
            {
                return now - record.timestamp > CHANGE_EXPIRATION_MS;
            }), history.end());

            if (history.empty())
            {
                it = s_changeHistory.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::array<bool, 8> collectHighlightMask(uint32_t id, uint32_t now, uint32_t &lastChangeTimestamp)
    {
        std::array<bool, 8> mask{};
        mask.fill(false);
        lastChangeTimestamp = 0;

        auto it = s_changeHistory.find(id);
        if (it == s_changeHistory.end())
        {
            return mask;
        }

        auto &history = it->second;
        history.erase(std::remove_if(history.begin(), history.end(), [now](const ChangeRecord &record)
        {
            return now - record.timestamp > CHANGE_EXPIRATION_MS;
        }), history.end());

        if (history.empty())
        {
            s_changeHistory.erase(it);
            return mask;
        }

        for (const auto &record : history)
        {
            if (record.byteIndex < mask.size())
            {
                mask[record.byteIndex] = true;
            }
            if (record.timestamp > lastChangeTimestamp)
            {
                lastChangeTimestamp = record.timestamp;
            }
        }

        return mask;
    }
}

AsyncWebServer WebInterface::server(80);
const std::map<uint32_t, CANMessage>* WebInterface::latestMessages = nullptr;
const std::map<uint32_t, CANMessage>* WebInterface::previousMessages = nullptr;
bool (*WebInterface::transmitCallback)(uint32_t id, uint8_t length, const uint8_t* data) = nullptr;

// HTML template moved from main.cpp
// Note: the page loads once and client-side JavaScript fetches table fragments
// so only the table bodies are updated (smoother UI than full page reload)
const char* WebInterface::HTML_TEMPLATE = R"html(
<!DOCTYPE html>
<html>
<head>
    <title>CAN Bus Monitor</title>
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <style>
        * { box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; padding: 0; margin: 0; background-color: #fafafa; color: #333; }
        
        /* Header and Navigation */
        header { background-color: #1a1a1a; color: white; padding: 0; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        .header-content { max-width: 1400px; margin: 0 auto; padding: 16px; }
        .app-title { font-size: 24px; font-weight: 700; margin: 0; color: white; }
        nav { background-color: #2d2d2d; }
        nav ul { list-style: none; margin: 0; padding: 0; display: flex; }
        nav li { margin: 0; }
        nav a { display: block; padding: 12px 20px; color: white; text-decoration: none; transition: background-color 200ms; border-bottom: 3px solid transparent; }
        nav a:hover { background-color: #3d3d3d; }
        nav a.active { background-color: #4caf50; border-bottom-color: #4caf50; }
        
        /* Main content */
        main { max-width: 1400px; margin: 0 auto; padding: 20px 16px; }
        .page { display: none; }
        .page.active { display: block; }
        
        h2 { margin: 20px 0 16px 0; color: #1a1a1a; }
        p { margin: 0 0 12px 0; }
        a { color: #1976d2; text-decoration: none; }
        a:hover { text-decoration: underline; }
        
        /* Sections */
        .section { margin: 20px 0; }
        
        /* Tables */
        table { border-collapse: collapse; width: 100%; background-color: white; border: 1px solid #ddd; border-radius: 4px; overflow: hidden; }
        th, td { border: 1px solid #ddd; padding: 12px; text-align: left; }
        th { background-color: #f5f5f5; font-weight: 600; }
        tbody { transition: opacity 120ms ease-in-out; }
        tbody tr { cursor: pointer; }
        tbody tr:hover { background-color: #f9f9f9; }
        
        /* Data highlighting */
        .highlight { background-color: #ffeb3b; }
        .byte { display: inline-block; min-width: 25px; font-family: monospace; }
        .age-fresh { color: #4caf50; font-weight: 500; }
        .age-medium { color: #ff9800; font-weight: 500; }
        .age-old { color: #f44336; font-weight: 500; }
        
        /* Forms */
        .transmit-section { background-color: white; border: 1px solid #ddd; padding: 20px; border-radius: 4px; margin-top: 20px; }
        .transmit-field { display: flex; flex-direction: column; }
        .transmit-field label { font-weight: 600; margin-bottom: 6px; font-size: 0.95em; color: #1a1a1a; }
        .transmit-field input { padding: 8px; border: 1px solid #ccc; border-radius: 3px; font-family: monospace; font-size: 14px; }
        .transmit-field input[type="number"] { width: 100px; }
        .transmit-field input[type="text"] { width: 150px; }
        .byte-input { width: 60px !important; text-align: center; text-transform: uppercase; letter-spacing: 1px; }
        
        /* Buttons */
        button { padding: 10px 16px; cursor: pointer; background-color: #4caf50; color: white; border: none; border-radius: 3px; font-weight: 600; font-size: 14px; transition: background-color 200ms; }
        button:hover { background-color: #45a049; }
        button:active { transform: scale(0.98); }
        
        /* Status messages */
        .status-message { margin-top: 12px; padding: 12px; border-radius: 3px; display: none; border-left: 4px solid; }
        .status-message.success { background-color: #d4edda; color: #155724; border-left-color: #28a745; }
        .status-message.error { background-color: #f8d7da; color: #721c24; border-left-color: #dc3545; }
        
        /* Filters section */
        .filters { background-color: white; border: 1px solid #ddd; padding: 16px; border-radius: 4px; margin-bottom: 20px; }
        .filter-actions { margin-bottom: 16px; display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
        .status { font-size: 0.95em; color: #666; }
        #id_list { display: flex; flex-wrap: wrap; gap: 16px; margin-top: 12px; }
        .id-option { display: flex; align-items: center; gap: 6px; }
        .id-option input { cursor: pointer; }
        .id-option span { cursor: pointer; user-select: none; }
    </style>
    <script>
        const POLL_MS = 1000; // refresh interval for the latest table (1000ms = 1 update per second)

        async function updateLatest()
        {
            try
            {
                const res = await fetch('/latest_messages', {cache: 'no-store'});
                if (!res.ok)
                {
                    console.error('Fetch failed', '/latest_messages', res.status);
                    return;
                }
                const text = await res.text();
                const el = document.getElementById('latest_body');
                if (!el) return;
                el.style.opacity = 0.2;
                requestAnimationFrame(() => {
                    el.innerHTML = text;
                    el.style.opacity = 1.0;
                    // Re-attach row click handlers after table update
                    attachRowClickHandlers();
                });
            }
            catch (e)
            {
                console.error('Error fetching latest messages', e);
            }
        }

        function attachRowClickHandlers()
        {
            const rows = document.querySelectorAll('#latest_body tr');
            rows.forEach(row => {
                row.addEventListener('click', () => {
                    const cells = row.querySelectorAll('td');
                    if (cells.length >= 3) {
                        const idCell = cells[0].textContent.trim(); // "0x..."
                        const lengthCell = parseInt(cells[1].textContent.trim());
                        const dataCell = cells[2].textContent.trim(); // "01 02 03 ..."
                        
                        // Parse ID (remove 0x)
                        const id = idCell.startsWith('0x') ? idCell.substring(2) : idCell;
                        
                        // Parse data bytes
                        const byteStrings = dataCell.split(/\s+/).filter(b => b.length > 0);
                        
                        // Populate transmit form
                        document.getElementById('tx_id').value = id;
                        document.getElementById('tx_length').value = lengthCell;
                        
                        // Clear all byte inputs first
                        for (let i = 0; i < 8; i++) {
                            document.getElementById('tx_byte_' + i).value = '';
                        }
                        
                        // Fill in the bytes
                        byteStrings.forEach((byte, index) => {
                            if (index < 8) {
                                document.getElementById('tx_byte_' + index).value = byte;
                            }
                        });
                        
                        // Update byte input active/inactive state based on loaded length
                        updateByteInputs();
                    }
                });
            });
        }

        function updateByteInputs()
        {
            const length = parseInt(document.getElementById('tx_length').value) || 0;
            const constrainedLength = Math.min(Math.max(length, 0), 8);
            document.getElementById('tx_length').value = constrainedLength;
            
            // All byte inputs are always visible, just update disabled state for clarity
            for (let i = 0; i < 8; i++) {
                const input = document.getElementById('tx_byte_' + i);
                if (i < constrainedLength) {
                    input.style.opacity = '1.0';
                    input.disabled = false;
                } else {
                    input.style.opacity = '0.5';
                    input.disabled = true;
                    input.value = '';
                }
            }
        }

        async function transmitMessage()
        {
            const id = document.getElementById('tx_id').value.trim();
            const length = parseInt(document.getElementById('tx_length').value) || 0;
            const statusEl = document.getElementById('transmit_status');
            
            if (!id) {
                statusEl.textContent = 'Error: ID is required';
                statusEl.className = 'status-message error';
                statusEl.style.display = 'block';
                return;
            }
            
            if (length < 0 || length > 8) {
                statusEl.textContent = 'Error: Length must be 0-8';
                statusEl.className = 'status-message error';
                statusEl.style.display = 'block';
                return;
            }
            
            const data = [];
            for (let i = 0; i < length; i++) {
                const byteVal = document.getElementById('tx_byte_' + i).value.trim();
                if (!byteVal) {
                    statusEl.textContent = 'Error: Byte ' + i + ' is required';
                    statusEl.className = 'status-message error';
                    statusEl.style.display = 'block';
                    return;
                }
                const parsed = parseInt(byteVal, 16);
                if (isNaN(parsed) || parsed < 0 || parsed > 255) {
                    statusEl.textContent = 'Error: Byte ' + i + ' must be valid hex (0-FF)';
                    statusEl.className = 'status-message error';
                    statusEl.style.display = 'block';
                    return;
                }
                data.push(parsed);
            }
            
            try {
                const res = await fetch('/transmit_message', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ id: id, length: length, data: data })
                });
                
                if (res.ok) {
                    statusEl.textContent = 'Message transmitted: ID=0x' + id + ', Length=' + length;
                    statusEl.className = 'status-message success';
                    statusEl.style.display = 'block';
                } else {
                    statusEl.textContent = 'Error: Transmit failed (HTTP ' + res.status + ')';
                    statusEl.className = 'status-message error';
                    statusEl.style.display = 'block';
                }
            } catch (e) {
                statusEl.textContent = 'Error: ' + e.message;
                statusEl.className = 'status-message error';
                statusEl.style.display = 'block';
            }
        }

        function switchPage(page)
        {
            // Hide all pages
            document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
            document.querySelectorAll('.nav-link').forEach(link => link.classList.remove('active'));
            
            // Show the selected page and update nav
            if (page === 'home') {
                document.getElementById('home-page').classList.add('active');
                document.getElementById('nav-home').classList.add('active');
            } else if (page === 'filter') {
                document.getElementById('filter-page').classList.add('active');
                document.getElementById('nav-filter').classList.add('active');
                // Initialize filter page if needed
                if (typeof startFilteredPage === 'function') {
                    startFilteredPage();
                }
            }
        }

        function startPolling()
        {
            updateLatest();
            setInterval(updateLatest, POLL_MS);
            // Initialize byte input display
            updateByteInputs();
        }

        window.addEventListener('load', () => {
            startPolling();
            // Pre-initialize the filter page data but keep it hidden
            if (typeof startFilteredPage === 'function') {
                startFilteredPage();
            }
        });
    </script>
</head>
<body>
    <header>
        <div class="header-content">
            <h1 class="app-title">RCLS CAN Bus Monitor</h1>
        </div>
    </header>
    <nav>
        <ul>
            <li><a href="#" onclick="switchPage('home'); return false;" class="nav-link active" id="nav-home">Home</a></li>
            <li><a href="#" onclick="switchPage('filter'); return false;" class="nav-link" id="nav-filter">Filter</a></li>
        </ul>
    </nav>
    <main>
        <div id="home-page" class="page active">
            <h2>Latest State</h2>
            <div class="section">
                <table>
                    <thead>
                        <tr>
                            <th>ID</th>
                            <th>Length</th>
                            <th>Data</th>
                            <th>Last Update</th>
                            <th>Age (ms)</th>
                        </tr>
                    </thead>
                    <tbody id="latest_body">
                        %LATEST_MESSAGES%
                    </tbody>
                </table>
            </div>

            <div class="transmit-section">
                <h2>Transmit Message</h2>
                <p style="font-size: 0.95em; color: #666;">Click a row above to copy its data, or enter values manually</p>
                <div style="display: flex; gap: 24px; margin-bottom: 20px; flex-wrap: wrap;">
                    <div class="transmit-field">
                        <label for="tx_id">ID (hex)</label>
                        <input type="text" id="tx_id" placeholder="123" />
                    </div>
                    <div class="transmit-field">
                        <label for="tx_length">Length (bytes)</label>
                        <input type="number" id="tx_length" min="0" max="8" value="1" onchange="updateByteInputs()" />
                    </div>
                </div>
                <div style="margin-bottom: 16px;">
                    <label style="font-weight: 600; display: block; margin-bottom: 12px; color: #1a1a1a;">Data (hex bytes)</label>
                    <div style="display: flex; gap: 12px; flex-wrap: wrap;">
                        <div class="transmit-field" style="margin: 0;">
                            <label style="font-weight: normal; font-size: 0.85em;">Byte 0</label>
                            <input type="text" id="tx_byte_0" class="byte-input" placeholder="00" />
                        </div>
                        <div class="transmit-field" style="margin: 0;">
                            <label style="font-weight: normal; font-size: 0.85em;">Byte 1</label>
                            <input type="text" id="tx_byte_1" class="byte-input" placeholder="00" />
                        </div>
                        <div class="transmit-field" style="margin: 0;">
                            <label style="font-weight: normal; font-size: 0.85em;">Byte 2</label>
                            <input type="text" id="tx_byte_2" class="byte-input" placeholder="00" />
                        </div>
                        <div class="transmit-field" style="margin: 0;">
                            <label style="font-weight: normal; font-size: 0.85em;">Byte 3</label>
                            <input type="text" id="tx_byte_3" class="byte-input" placeholder="00" />
                        </div>
                        <div class="transmit-field" style="margin: 0;">
                            <label style="font-weight: normal; font-size: 0.85em;">Byte 4</label>
                            <input type="text" id="tx_byte_4" class="byte-input" placeholder="00" />
                        </div>
                        <div class="transmit-field" style="margin: 0;">
                            <label style="font-weight: normal; font-size: 0.85em;">Byte 5</label>
                            <input type="text" id="tx_byte_5" class="byte-input" placeholder="00" />
                        </div>
                        <div class="transmit-field" style="margin: 0;">
                            <label style="font-weight: normal; font-size: 0.85em;">Byte 6</label>
                            <input type="text" id="tx_byte_6" class="byte-input" placeholder="00" />
                        </div>
                        <div class="transmit-field" style="margin: 0;">
                            <label style="font-weight: normal; font-size: 0.85em;">Byte 7</label>
                            <input type="text" id="tx_byte_7" class="byte-input" placeholder="00" />
                        </div>
                    </div>
                </div>
                <button onclick="transmitMessage()">Transmit</button>
                <div id="transmit_status" class="status-message"></div>
            </div>
        </div>

        <div id="filter-page" class="page">
            <h2>Filtered Recent Messages</h2>
            <div class="filters">
                <div class="filter-actions">
                    <button onclick="setAll(true)">Select All</button>
                    <button onclick="setAll(false)">Clear All</button>
                    <span class="status">Tracking <span id="id_count">0</span> IDs</span>
                </div>
                <div id="id_list"></div>
            </div>
            <table>
                <thead>
                    <tr>
                        <th>ID</th>
                        <th>Length</th>
                        <th>Data</th>
                        <th>RX Time (ms)</th>
                        <th>Age (ms)</th>
                    </tr>
                </thead>
                <tbody id="filtered_body"></tbody>
            </table>
        </div>
    </main>
    <script>
        const GRID_POLL_MS = 1000; // refresh interval for filtered table (1000ms = 1 update per second)
        const ID_REFRESH_MS = 3000; // refresh interval for ID list (3000ms = every 3 seconds)
        let selectedIds = new Set();
        let lastIdRefresh = 0;
        let filteredPageIntervals = { gridInterval: null, idInterval: null };
        let filteredPageInitialized = false;

        async function fetchIds()
        {
            const now = Date.now();
            if (now - lastIdRefresh < ID_REFRESH_MS) {
                return;
            }
            lastIdRefresh = now;
            try
            {
                const res = await fetch('/filtered_ids', {cache: 'no-store'});
                if (!res.ok) return;
                const ids = await res.json();
                renderIdList(ids);
            }
            catch (e)
            {
                console.error('Failed to fetch IDs', e);
            }
        }

        function renderIdList(ids)
        {
            const container = document.getElementById('id_list');
            const previousSelection = new Set(selectedIds);
            const hadManualSelection = previousSelection.size > 0;
            container.innerHTML = '';
            ids.forEach(id => {
                const label = document.createElement('label');
                label.className = 'id-option';
                const checkbox = document.createElement('input');
                checkbox.type = 'checkbox';
                checkbox.value = id;
                const shouldCheck = !hadManualSelection || previousSelection.has(id);
                checkbox.checked = shouldCheck;
                if (shouldCheck) {
                    selectedIds.add(id);
                } else {
                    selectedIds.delete(id);
                }
                checkbox.addEventListener('change', () => {
                    if (checkbox.checked) {
                        selectedIds.add(id);
                    } else {
                        selectedIds.delete(id);
                    }
                    fetchFilteredMessages();
                });
                const text = document.createElement('span');
                text.textContent = id;
                label.appendChild(checkbox);
                label.appendChild(text);
                container.appendChild(label);
            });
            if (!hadManualSelection && ids.length)
            {
                selectedIds = new Set(ids);
                document.querySelectorAll('#id_list input[type=checkbox]').forEach(cb => cb.checked = true);
            }
            document.getElementById('id_count').textContent = ids.length;
        }

        function setAll(state)
        {
            selectedIds = state ? new Set(Array.from(document.querySelectorAll('#id_list input')).map(cb => cb.value))
                                : new Set();
            document.querySelectorAll('#id_list input').forEach(cb => cb.checked = state);
            fetchFilteredMessages();
        }

        function getSelectedIdsParam()
        {
            if (selectedIds.size === 0) {
                return '';
            }
            return Array.from(selectedIds).join(',');
        }

        async function fetchFilteredMessages()
        {
            try
            {
                const idsParam = getSelectedIdsParam();
                const url = '/filtered_messages?ids=' + encodeURIComponent(idsParam);
                const res = await fetch(url, {cache: 'no-store'});
                if (!res.ok) return;
                const html = await res.text();
                const body = document.getElementById('filtered_body');
                body.style.opacity = 0.2;
                requestAnimationFrame(() => {
                    body.innerHTML = html;
                    body.style.opacity = 1.0;
                });
            }
            catch (e)
            {
                console.error('Failed to fetch filtered messages', e);
            }
        }

        function startFilteredPage()
        {
            // Prevent multiple initializations
            if (filteredPageInitialized) {
                return;
            }
            filteredPageInitialized = true;
            
            // Clear any existing intervals first
            if (filteredPageIntervals.gridInterval !== null) {
                clearInterval(filteredPageIntervals.gridInterval);
            }
            if (filteredPageIntervals.idInterval !== null) {
                clearInterval(filteredPageIntervals.idInterval);
            }
            
            // Fetch initial data
            fetchIds().then(fetchFilteredMessages);
            
            // Set up new intervals
            filteredPageIntervals.gridInterval = setInterval(fetchFilteredMessages, GRID_POLL_MS);
            filteredPageIntervals.idInterval = setInterval(fetchIds, ID_REFRESH_MS);
        }

        window.addEventListener('load', startFilteredPage);
    </script>
</head>
<body>
</body>
</html>
)html";


/*

    <h2>Filtered Recent Messages</h2>
    <p><a href="/">Back to main dashboard</a></p>
    <div class="filters">
        <div class="filter-actions">
            <button onclick="setAll(true)">Select All</button>
            <button onclick="setAll(false)">Clear All</button>
            <span class="status">Tracking <span id="id_count">0</span> IDs</span>
        </div>
        <div id="id_list"></div>
    </div>
    <table>
        <thead>
            <tr>
                <th>ID</th>
                <th>Length</th>
                <th>Data</th>
                <th>RX Time (ms)</th>
                <th>Age (ms)</th>
            </tr>
        </thead>
        <tbody id="filtered_body"></tbody>
    </table>
*/

// FILTERED_TEMPLATE is now part of HTML_TEMPLATE with client-side navigation
// This constant is kept for backward compatibility with generateFilteredPage()
const char* WebInterface::FILTERED_TEMPLATE = WebInterface::HTML_TEMPLATE;

bool WebInterface::initialize(const char* ssid, const char* password)
{
    // Connect to WiFi
    WiFi.setHostname("RCLS-CAN");
    WiFi.begin(ssid, password);
    uint8_t retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20)
    {
        delay(1000);
        Serial.print("Connecting to WiFi: ");
        Serial.print(ssid);
        Serial.print(" ");
        Serial.println(password);
        retries++;
    }
    
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Failed to connect to WiFi");
        return false;
    }
    
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    // Setup web server
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        request->send(200, "text/html", generateHtml());
    });

    server.on("/latest_messages", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        request->send(200, "text/html", generateLatestRows());
    });
    server.on("/filtered", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        request->send(200, "text/html", generateFilteredPage());
    });
    server.on("/filtered_ids", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        request->send(200, "application/json", generateIdListJson());
    });
    server.on("/filtered_messages", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        String rawIds;
        if (request->hasParam("ids"))
        {
            rawIds = request->getParam("ids")->value();
        }
        auto ids = parseIdList(rawIds);
        request->send(200, "text/html", generateFilteredRows(ids));
    });
    server.on("/transmit_message", HTTP_POST, [](AsyncWebServerRequest *request)
    {
        if (request->hasParam("body", true))
        {
            // This will be handled in onBody
        }
        request->send(400, "application/json", "{\"error\":\"Invalid request\"}");
    }, nullptr, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
    {
        // onBody handler for JSON parsing
        static String jsonBody;
        
        // Accumulate body data
        jsonBody = String((char *)data);
        
        // Simple JSON parsing (since we don't have a JSON library readily available)
        // Expected format: {"id":"123","length":2,"data":[0x12,0x34]}
        // Extract id
        int idStart = jsonBody.indexOf("\"id\":\"") + 6;
        int idEnd = jsonBody.indexOf("\"", idStart);
        String idStr = jsonBody.substring(idStart, idEnd);
        
        // Extract length
        int lenStart = jsonBody.indexOf("\"length\":") + 9;
        int lenEnd = jsonBody.indexOf(",", lenStart);
        if (lenEnd == -1) lenEnd = jsonBody.indexOf("}", lenStart);
        String lenStr = jsonBody.substring(lenStart, lenEnd);
        
        // Extract data array
        int dataStart = jsonBody.indexOf("\"data\":[") + 8;
        int dataEnd = jsonBody.indexOf("]", dataStart);
        String dataStr = jsonBody.substring(dataStart, dataEnd);
        
        // Parse ID
        uint32_t id = strtoul(idStr.c_str(), nullptr, 16);
        uint8_t length = atoi(lenStr.c_str());
        
        // Parse data bytes
        uint8_t dataBytes[8];
        int byteCount = 0;
        int pos = 0;
        while (pos < dataStr.length() && byteCount < 8)
        {
            int commaPos = dataStr.indexOf(',', pos);
            if (commaPos == -1) commaPos = dataStr.length();
            
            String byteStr = dataStr.substring(pos, commaPos);
            byteStr.trim();
            dataBytes[byteCount] = strtoul(byteStr.c_str(), nullptr, 0);
            byteCount++;
            pos = commaPos + 1;
        }
        
        // Call transmit callback if set
        if (transmitCallback && byteCount >= length)
        {
            if (transmitCallback(id, length, dataBytes))
            {
                request->send(200, "application/json", "{\"status\":\"transmitted\"}");
            }
            else
            {
                request->send(500, "application/json", "{\"error\":\"Transmit failed\"}");
            }
        }
        else
        {
            request->send(400, "application/json", "{\"error\":\"Invalid parameters\"}");
        }
        
        jsonBody = "";
    });

    server.begin();
    Serial.println("Web server started");
    return true;
}

void WebInterface::setMessageMaps(
    const std::map<uint32_t, CANMessage>* latest,
    const std::map<uint32_t, CANMessage>* previous)
{
    latestMessages = latest;
    previousMessages = previous;
}

void WebInterface::setTransmitCallback(bool (*callback)(uint32_t id, uint8_t length, const uint8_t* data))
{
    transmitCallback = callback;
}

void WebInterface::recordChange(const CANMessage& current, const CANMessage* previous)
{
    uint32_t now = millis();
    pruneHistoryForId(current.id, now);

    std::vector<ChangeRecord> newRecords;
    newRecords.reserve(current.length);

    bool lengthChanged = !previous || previous->length != current.length;

    for (uint8_t i = 0; i < current.length; ++i)
    {
        bool valueChanged = !previous || i >= previous->length || current.data[i] != previous->data[i];
        if (lengthChanged || valueChanged)
        {
            ChangeRecord record;
            record.timestamp = now;
            record.byteIndex = i;
            record.newValue = current.data[i];
            record.oldValue = (previous && i < previous->length) ? previous->data[i] : current.data[i];
            newRecords.push_back(record);
        }
    }

    if (!newRecords.empty())
    {
        auto &history = s_changeHistory[current.id];
        history.insert(history.end(), newRecords.begin(), newRecords.end());
    }
}

String WebInterface::formatByte(uint8_t byte, bool highlight)
{
    String result = "<span class='byte";
    if (highlight)
    {
        result += " highlight";
    }
    result += "'>";
    if (byte < 0x10)
    {
        result += "0";
    }
    result += String(byte, HEX);
    result += "</span> ";
    return result;
}

String WebInterface::generateHtml()
{
    if (!latestMessages)
    {
        return "Error: Message maps not initialized";
    }

    String html = HTML_TEMPLATE;
    html.replace("%LATEST_MESSAGES%", generateLatestRows());
    return html;
}

String WebInterface::generateLatestRows()
{
    if (!latestMessages)
    {
        return "";
    }

    uint32_t currentTime = millis();
    String latestRows;
    for (const auto& pair : *latestMessages)
    {
        uint32_t lastChangeTimestamp = 0;
        auto highlightMask = collectHighlightMask(pair.first, currentTime, lastChangeTimestamp);

        bool hasPrev = false;
        std::map<uint32_t, CANMessage>::const_iterator prevIt;
        if (previousMessages)
        {
            prevIt = previousMessages->find(pair.first);
            hasPrev = prevIt != previousMessages->end();
        }

        String row = "<tr><td>0x" + String(pair.first, HEX) + "</td>";
        row += "<td>" + String(pair.second.length) + "</td><td>";

        for (int i = 0; i < pair.second.length; i++)
        {
            bool highlight = (i < static_cast<int>(highlightMask.size())) ? highlightMask[i] : false;
            if (!highlight && previousMessages && hasPrev)
            {
                highlight = i >= prevIt->second.length ||
                            pair.second.data[i] != prevIt->second.data[i];
            }

            row += formatByte(pair.second.data[i], highlight);
        }

        uint32_t age = currentTime - pair.second.timestamp;
        String ageClass;
        if (age < 1000) ageClass = "age-fresh";          // Less than 1 second
        else if (age < 5000) ageClass = "age-medium";    // Less than 5 seconds
        else ageClass = "age-old";                       // More than 5 seconds

        row += "</td><td>" + String(pair.second.timestamp) +
               "</td><td class='" + ageClass + "'>" + String(age) + "</td></tr>\n";
        latestRows += row;
    }

    return latestRows;
}

String WebInterface::generateFilteredPage()
{
    return FILTERED_TEMPLATE;
}

String WebInterface::generateIdListJson()
{
    if (!latestMessages)
    {
        return "[]";
    }

    uint32_t now = millis();
    pruneAllHistory(now);

    std::vector<uint32_t> ids;
    ids.reserve(s_changeHistory.size());
    // Build the ID list from the overall latestMessages map so the
    // filtered page shows all known IDs regardless of change age.
    ids.reserve(latestMessages->size());
    for (const auto& entry : *latestMessages)
    {
        ids.push_back(entry.first);
    }

    std::sort(ids.begin(), ids.end());

    String json = "[";
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (i != 0)
        {
            json += ",";
        }
        json += "\"0x";
        json += String(ids[i], HEX);
        json += "\"";
    }
    json += "]";
    return json;
}

std::vector<uint32_t> WebInterface::parseIdList(const String& rawIds)
{
    std::vector<uint32_t> ids;
    if (!rawIds.length())
    {
        return ids;
    }

    int start = 0;
    while (start <= rawIds.length())
    {
        int end = rawIds.indexOf(',', start);
        if (end == -1)
        {
            end = rawIds.length();
        }
        String token = rawIds.substring(start, end);
        token.trim();
        if (token.length() > 0)
        {
            token.toUpperCase();
            if (token.startsWith("0X"))
            {
                token = token.substring(2);
            }
            char* endPtr = nullptr;
            uint32_t value = strtoul(token.c_str(), &endPtr, 16);
            if (endPtr != token.c_str())
            {
                ids.push_back(value);
            }
        }
        start = end + 1;
    }
    return ids;
}

String WebInterface::generateFilteredRows(const std::vector<uint32_t>& ids)
{
    if (!latestMessages)
    {
        return "<tr><td colspan='5'>Waiting for CAN data...</td></tr>";
    }

    if (ids.empty())
    {
        return "<tr><td colspan='5'>No IDs selected</td></tr>";
    }

    std::set<uint32_t> filter(ids.begin(), ids.end());
    String rows;
    uint32_t currentTime = millis();

    for (const auto& pair : *latestMessages)
    {
        if (filter.find(pair.first) == filter.end())
        {
            continue;
        }

        uint32_t lastChangeTimestamp = 0;
        auto highlightMask = collectHighlightMask(pair.first, currentTime, lastChangeTimestamp);
        if (lastChangeTimestamp == 0)
        {
            continue;
        }

        uint32_t age = currentTime - lastChangeTimestamp;
        if (age > CHANGE_EXPIRATION_MS)
        {
            continue;
        }

        bool hasPrev = false;
        std::map<uint32_t, CANMessage>::const_iterator prevIt;
        if (previousMessages)
        {
            prevIt = previousMessages->find(pair.first);
            hasPrev = prevIt != previousMessages->end();
        }

        rows += "<tr><td>0x" + String(pair.first, HEX) + "</td>";
        rows += "<td>" + String(pair.second.length) + "</td><td>";

        for (int i = 0; i < pair.second.length; i++)
        {
            bool highlight = (i < static_cast<int>(highlightMask.size())) ? highlightMask[i] : false;
            if (!highlight && previousMessages && hasPrev)
            {
                highlight = i >= prevIt->second.length ||
                            pair.second.data[i] != prevIt->second.data[i];
            }
            rows += formatByte(pair.second.data[i], highlight);
        }

        String ageClass;
        if (age < 1000) ageClass = "age-fresh";
        else if (age < 5000) ageClass = "age-medium";
        else ageClass = "age-old";

        rows += "</td><td>" + String(pair.second.timestamp) +
                "</td><td class='" + ageClass + "'>" + String(age) + "</td></tr>\n";
    }

    if (!rows.length())
    {
        rows = "<tr><td colspan='5'>No matching IDs found or messages have expired</td></tr>";
    }

    return rows;
}
