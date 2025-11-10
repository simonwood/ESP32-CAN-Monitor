#include "web_interface.h"

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

bool WebInterface::initialize(const char* ssid, const char* password)
{
    // Connect to WiFi
    WiFi.begin(ssid, password);
    uint8_t retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20)
    {
        delay(1000);
        Serial.println("Connecting to WiFi...");
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