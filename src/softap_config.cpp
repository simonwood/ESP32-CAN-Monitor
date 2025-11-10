#include "softap_config.h"

AsyncWebServer SoftAPConfig::server(80);
DNSServer SoftAPConfig::dnsServer;
Preferences SoftAPConfig::preferences;

String generateUniqueSSID() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[16];
    snprintf(ssid, sizeof(ssid), "RCLS-%02X%02X%02X", mac[3], mac[4], mac[5]);
    return String(ssid);
}

const String uniqueSSID = generateUniqueSSID();
const char* SoftAPConfig::AP_SSID = uniqueSSID.c_str();
const char* SoftAPConfig::AP_PASSWORD = "configure";
const char* SoftAPConfig::PORTAL_HOSTNAME = "rcls.config";

bool SoftAPConfig::checkConfigMode()
{
    // Setup pin with internal pull-up
    pinMode(CONFIG_PIN, INPUT_PULLUP);
    delay(10); // Short delay for pin to stabilize
    
    // Check if button is pressed (LOW) at boot
    bool configMode = (digitalRead(CONFIG_PIN) == LOW);
    
    if (configMode)
    {
        Serial.println("Config button pressed at boot - entering configuration mode");
    }
    
    return configMode;
}

bool SoftAPConfig::startConfigPortal()
{
    // Start SoftAP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    
    // Start DNS server for captive portal
    startDNSServer();
    
    // Setup web server
    setupConfigPage();
    
    server.begin();
    Serial.println("Configuration portal started");
    
    // Stay in config mode until reboot
    while (true)
    {
        dnsServer.processNextRequest();
        delay(10);
    }
    
    return true; // Never reached
}

void SoftAPConfig::startDNSServer()
{
    // Route all DNS requests to the AP IP
    dnsServer.start(53, "*", WiFi.softAPIP());
}

void SoftAPConfig::setupConfigPage()
{
    // Serve configuration page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", generateHTML());
    });
    
    // Handle form submission
    server.on("/save", HTTP_POST, handleConfigSave);
    
    // Captive portal detection responses
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request){
        request->redirect("/");
    });
    
    server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request){
        request->redirect("/");
    });
}

void SoftAPConfig::handleConfigSave(AsyncWebServerRequest* request)
{
    Config config;
    bool valid = false;
    
    if (request->hasParam("ssid", true) && request->hasParam("password", true))
    {
        const AsyncWebParameter* ssidParam = request->getParam("ssid", true);
        const AsyncWebParameter* passParam = request->getParam("password", true);

        Serial.print("SSID: ");
        Serial.println(ssidParam->value());     
        Serial.print("Password: ");
        Serial.println(passParam->value());
        
        if (ssidParam->value().length() < 33 && passParam->value().length() < 65)
        {
            strncpy(config.ssid, ssidParam->value().c_str(), sizeof(config.ssid) - 1);
            strncpy(config.password, passParam->value().c_str(), sizeof(config.password) - 1);
            config.ssid[sizeof(config.ssid) - 1] = '\0';
            config.password[sizeof(config.password) - 1] = '\0';
            valid = true;
            Serial.println("Received valid configuration");
        }
    }
    
    String response;
    if (valid && saveConfig(config))
    {
        response = "Configuration saved successfully! Please power cycle the device.";
    }
    else
    {
        response = "Error saving configuration. Please try again.";
    }
    
    request->send(200, "text/html", response);
}

String SoftAPConfig::generateHTML()
{
    Config currentConfig;
    loadConfig(currentConfig);
    
    String html = R"html(
<!DOCTYPE html>
<html>
<head>
    <title>)html";
    html += String(AP_SSID);
    html += R"html( Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }
        .container { max-width: 400px; margin: 0 auto; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; }
        input[type="text"],
        input[type="password"] {
            width: 100%;
            padding: 8px;
            border: 1px solid #ddd;
            border-radius: 4px;
            box-sizing: border-box;
        }
        button {
            background-color: #4CAF50;
            color: white;
            padding: 10px 15px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            width: 100%;
        }
        button:hover { background-color: #45a049; }
        .note { 
            background-color: #fff3cd;
            padding: 10px;
            border-radius: 4px;
            margin-top: 20px;
            font-size: 0.9em;
        }
        .password-container {
            display: flex;
            gap: 8px;
        }
        .password-container input {
            flex: 1;
        }
        .show-pwd {
            background-color: #6c757d;
            color: white;
            padding: 8px 12px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            font-size: 14px;
            width: auto;
        }
        .show-pwd:hover {
            background-color: #5a6268;
        }
        .show-pwd.active {
            background-color: #0056b3;
        }
    </style>
    <script>
        function togglePassword() {
            const pwdField = document.getElementById('password');
            const button = document.querySelector('.show-pwd');
            if (pwdField.type === 'password') {
                pwdField.type = 'text';
                button.textContent = 'Hide';
                button.classList.add('active');
            } else {
                pwdField.type = 'password';
                button.textContent = 'Show';
                button.classList.remove('active');
            }
        }
    </script>
</head>
<body>
    <div class="container">
        <h2>)html";
    html += String(AP_SSID);
    html += R"html( Configuration</h2>
        <form action="/save" method="POST">
            <div class="form-group">
                <label for="ssid">WiFi Network Name (SSID):</label>
                <input type="text" id="ssid" name="ssid" value=")html";
    html += String(currentConfig.ssid);
    html += R"html(" required>
            </div>
            <div class="form-group">
                <label for="password">WiFi Password:</label>
                <div class="password-container">
                    <input type="password" id="password" name="password" value=")html";
    html += String(currentConfig.password);
    html += R"html(" required>
                    <button type="button" onclick="togglePassword()" class="show-pwd">Show</button>
                </div>
            </div>
            <button type="submit">Save Configuration</button>
        </form>
        <div class="note">
            <strong>Note:</strong> After saving, the device will need to be power cycled to apply the new configuration.
        </div>
    </div>
</body>
</html>)html";

    return html;
}

bool SoftAPConfig::loadConfig(Config& config)
{
    preferences.begin("vcmaster", true); // Read-only mode
    
    size_t ssidLen = preferences.getString("wifi_ssid", config.ssid, sizeof(config.ssid));
    size_t passLen = preferences.getString("wifi_pass", config.password, sizeof(config.password));
    
    preferences.end();
    
    return (ssidLen > 0 && passLen > 0);
}

bool SoftAPConfig::saveConfig(const Config& config)
{
    preferences.begin("vcmaster", false); // Read-write mode
    
    bool success = true;
    success &= preferences.putString("wifi_ssid", config.ssid);
    success &= preferences.putString("wifi_pass", config.password);
    
    preferences.end();
    
    return true;
}