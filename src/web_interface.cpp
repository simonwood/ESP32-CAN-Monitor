#include "web_interface.h"
#include <set>
#include <stdlib.h>
#include <cstring>

namespace
{
    constexpr uint32_t FILTERED_MESSAGE_TIMEOUT_MS = 10'000;

    struct TrackedChange
    {
        uint32_t lastChangeTimestamp = 0;
        uint8_t length = 0;
        uint8_t data[8] = {};
        bool initialized = false;
    };

    std::map<uint32_t, TrackedChange> s_filteredChangeTracker;
}

AsyncWebServer WebInterface::server(80);
const std::map<uint32_t, CANMessage>* WebInterface::recentMessages = nullptr;
const std::map<uint32_t, CANMessage>* WebInterface::latestMessages = nullptr;
const std::map<uint32_t, CANMessage>* WebInterface::previousMessages = nullptr;

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
        table { border-collapse: collapse; width: 100%; }
        th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }
        th { background-color: #f2f2f2; }
        .highlight { background-color: #ffeb3b; }
        .section { margin: 20px 0; }
        .byte { display: inline-block; min-width: 25px; }
        .age-fresh { color: green; }
        .age-medium { color: orange; }
        .age-old { color: red; }
        /* small smoothing to avoid jank when replacing tbody */
        tbody { transition: opacity 120ms ease-in-out; }
    </style>
    <style>
        .nav-link { margin-bottom: 16px; display: inline-block; color: #1976d2; text-decoration: none; }
        .nav-link:hover { text-decoration: underline; }
    </style>
    <script>
        // Poll endpoints for table fragments and replace tbody contents
        const POLL_MS = 600; // refresh interval; adjust for smoothness

        async function fetchAndUpdate(path, elementId)
        {
            try
            {
                const res = await fetch(path, {cache: 'no-store'});
                if (!res.ok)
                {
                    console.error('Fetch failed', path, res.status);
                    return;
                }
                const text = await res.text();
                const el = document.getElementById(elementId);
                if (!el) return;
                // Small fade to reduce visual jump
                el.style.opacity = 0.2;
                requestAnimationFrame(() => {
                    el.innerHTML = text;
                    el.style.opacity = 1.0;
                });
            }
            catch (e)
            {
                console.error('Error fetching', path, e);
            }
        }

        function startPolling()
        {
            // Initial load
            fetchAndUpdate('/recent_messages', 'recent_body');
            fetchAndUpdate('/latest_messages', 'latest_body');
            // Polling
            setInterval(() => {
                fetchAndUpdate('/recent_messages', 'recent_body');
                fetchAndUpdate('/latest_messages', 'latest_body');
            }, POLL_MS);
        }

        window.addEventListener('load', startPolling);
    </script>
</head>
<body>
    <a class="nav-link" href="/filtered">Open filtered recent view</a>
    <h2>Recent Messages</h2>
    <div class="section">
        <table>
            <thead>
                <tr>
                    <th>Time (ms)</th>
                    <th>ID</th>
                    <th>Length</th>
                    <th>Data</th>
                </tr>
            </thead>
            <tbody id="recent_body">
                %RECENT_MESSAGES%
            </tbody>
        </table>
    </div>
    
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
</body>
</html>
)html";

const char* WebInterface::FILTERED_TEMPLATE = R"html(
<!DOCTYPE html>
<html>
<head>
    <title>Filtered CAN Messages</title>
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; padding: 16px; }
        h2 { margin-top: 0; }
        .filters { margin-bottom: 16px; }
        .id-option { display: inline-flex; align-items: center; margin: 4px 12px 4px 0; }
        .id-option input { margin-right: 6px; }
        .filter-actions { margin-bottom: 12px; }
        table { border-collapse: collapse; width: 100%; }
        th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }
        th { background-color: #f2f2f2; }
        .highlight { background-color: #ffeb3b; }
        .byte { display: inline-block; min-width: 25px; }
        .age-fresh { color: green; }
        .age-medium { color: orange; }
        .age-old { color: red; }
        #filtered_body { transition: opacity 120ms ease-in-out; }
        button { cursor: pointer; }
        .status { font-size: 0.9em; color: #666; margin-top: 8px; }
    </style>
    <script>
        const GRID_POLL_MS = 600;
        const ID_REFRESH_MS = 3000;
        let selectedIds = new Set();
        let lastIdRefresh = 0;

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
            fetchIds().then(fetchFilteredMessages);
            setInterval(fetchFilteredMessages, GRID_POLL_MS);
            setInterval(fetchIds, ID_REFRESH_MS);
        }

        window.addEventListener('load', startFilteredPage);
    </script>
</head>
<body>
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
</body>
</html>
)html";

bool WebInterface::initialize(const char* ssid, const char* password)
{
    // Connect to WiFi
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

    // Endpoints that return only the table body HTML for smooth updates
    server.on("/recent_messages", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        request->send(200, "text/html", generateRecentRows());
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

    server.begin();
    Serial.println("Web server started");
    return true;
}

void WebInterface::setMessageMaps(
    const std::map<uint32_t, CANMessage>* recent,
    const std::map<uint32_t, CANMessage>* latest,
    const std::map<uint32_t, CANMessage>* previous)
{
    recentMessages = recent;
    latestMessages = latest;
    previousMessages = previous;
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
    if (!recentMessages || !latestMessages || !previousMessages)
    {
        return "Error: Message maps not initialized";
    }

    String html = HTML_TEMPLATE;
    html.replace("%RECENT_MESSAGES%", generateRecentRows());
    html.replace("%LATEST_MESSAGES%", generateLatestRows());
    return html;
}

String WebInterface::generateRecentRows()
{
    if (!recentMessages)
    {
        return "";
    }
    String recentRows;
    for (auto it = recentMessages->rbegin(); it != recentMessages->rend(); ++it)
    {
        const CANMessage &m = it->second;
        recentRows += "<tr><td>" + String(m.timestamp) +
                      "</td><td>0x" + String(m.id, HEX) +
                      "</td><td>" + String(m.length) +
                      "</td><td>";

        // Format each byte individually
        for (int i = 0; i < m.length; i++)
        {
            recentRows += formatByte(m.data[i], false);
        }
        recentRows += "</td></tr>\n";
    }
    return recentRows;
}

String WebInterface::generateLatestRows()
{
    if (!latestMessages || !previousMessages)
    {
        return "";
    }

    String latestRows;
    for (const auto& pair : *latestMessages)
    {
        String row = "<tr><td>0x" + String(pair.first, HEX) + "</td>";
        row += "<td>" + String(pair.second.length) + "</td><td>";

        // Compare with previous message byte by byte
        auto prevIt = previousMessages->find(pair.first);
        for (int i = 0; i < pair.second.length; i++)
        {
            bool byteChanged = prevIt == previousMessages->end() ||
                               i >= prevIt->second.length ||
                               pair.second.data[i] != prevIt->second.data[i];

            row += formatByte(pair.second.data[i], byteChanged);
        }
        uint32_t currentTime = millis();
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

    String json = "[";
    bool first = true;
    for (const auto& pair : *latestMessages)
    {
        if (!first)
        {
            json += ",";
        }
        json += "\"0x";
        json += String(pair.first, HEX);
        json += "\"";
        first = false;
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
    if (!latestMessages || !previousMessages)
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

        auto& tracker = s_filteredChangeTracker[pair.first];
        bool dataChanged = !tracker.initialized ||
                           tracker.length != pair.second.length ||
                           memcmp(tracker.data, pair.second.data, pair.second.length) != 0;

        if (dataChanged)
        {
            tracker.length = pair.second.length;
            memcpy(tracker.data, pair.second.data, pair.second.length);
            tracker.lastChangeTimestamp = pair.second.timestamp;
            tracker.initialized = true;
        }

        uint32_t lastChangeTimestamp = tracker.initialized ? tracker.lastChangeTimestamp : pair.second.timestamp;
        uint32_t age = currentTime - lastChangeTimestamp;

        if (age > FILTERED_MESSAGE_TIMEOUT_MS)
        {
            continue;
        }

        rows += "<tr><td>0x" + String(pair.first, HEX) + "</td>";
        rows += "<td>" + String(pair.second.length) + "</td><td>";

        auto prevIt = previousMessages->find(pair.first);
        for (int i = 0; i < pair.second.length; i++)
        {
            bool byteChanged = prevIt == previousMessages->end() ||
                               i >= prevIt->second.length ||
                               pair.second.data[i] != prevIt->second.data[i];
            rows += formatByte(pair.second.data[i], byteChanged);
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
