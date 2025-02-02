#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <DHT.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// Definições de pins
#define DHTPIN 13
#define NUM_SOIL_SENSORS 2
#define SOIL_SENSOR1 12
#define SOIL_SENSOR2 14
#define RELAY_PIN 15
#define BTN_MODE 27

// Constantes
#define MQTT_RETRY_INTERVAL 30000         // 30 segundos entre tentativas de reconexão
#define DURACAO_MAXIMA_IRRIGACAO 1200000  // 20 minutos em millisegundos


// Substitua a função getESPTemp() por esta versão atualizada:
#ifdef __cplusplus
extern "C" {
#endif
  uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

float getESPTemp() {
#ifdef CONFIG_IDF_TARGET_ESP32
  // Para ESP32 original
  return (float)(140 - ((255 - temprature_sens_read()) * 165)) / 255;
#else
  // Para outros modelos de ESP32 (S2, S3, C3)
  return 0;  // Estes modelos não têm sensor de temperatura interno
#endif
}

// Objetos globais
WiFiManager wm;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -10800, 60000);
DHT dht(DHTPIN, DHT11);
Preferences preferences;

// Estruturas de dados
struct Config {
  char mqtt_server[40] = "";
  char mqtt_port[6] = "1883";
  char mqtt_user[40] = "";
  char mqtt_password[40] = "";
  int horaInicio = 5;
  int minutoInicio = 0;
  bool modoAutomatico = true;
} config;

struct SystemState {
  float temperatura = 0;
  float umidade = 0;
  float umidadeSolo = 0;
  bool relayStatus = false;
  unsigned long tempoIrrigacaoInicio = 0;
  unsigned long lastMqttRetry = 0;
} state;

// HTML da interface web
const char* htmlPage = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <title>Sistema de Irrigação</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { 
            font-family: Arial, sans-serif; 
            margin: 20px;
            background-color: #f0f2f5;
        }
        .container { 
            max-width: 1000px; 
            margin: auto;
            background-color: white;
            padding: 20px;
            border-radius: 12px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        .tab { 
            overflow: hidden; 
            border: none;
            background-color: #f8f9fa;
            border-radius: 8px;
            margin-bottom: 20px;
        }
        .tab button { 
            background-color: transparent; 
            float: left; 
            border: none; 
            padding: 14px 24px;
            cursor: pointer;
            font-size: 16px;
            border-radius: 8px;
            margin: 5px;
            transition: all 0.3s ease;
        }
        .tab button:hover { 
            background-color: #e9ecef;
        }
        .tab button.active { 
            background-color: #2196F3;
            color: white;
        }
        .tabcontent { 
            display: none; 
            padding: 20px;
            animation: fadeIn 0.5s;
        }
        @keyframes fadeIn {
            from {opacity: 0;}
            to {opacity: 1;}
        }
        .grid { 
            display: grid; 
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 20px;
            margin-bottom: 20px;
        }
        .card {
            background-color: #f8f9fa;
            padding: 20px;
            border-radius: 12px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.05);
        }
        .card h3 {
            margin: 0 0 15px 0;
            color: #333;
        }
        .card p {
            font-size: 1.2em;
            color: #2196F3;
            margin: 10px 0;
        }
        .config-section {
            background-color: #f8f9fa;
            padding: 20px;
            border-radius: 12px;
            margin-bottom: 20px;
        }
        .config-section h3 {
            margin: 0 0 15px 0;
            color: #333;
        }
        .form-group { 
            margin-bottom: 15px; 
        }
        label { 
            display: block; 
            margin-bottom: 8px;
            color: #555;
        }
        input[type="text"], 
        input[type="password"],
        input[type="number"] { 
            width: 100%;
            padding: 10px;
            border: 1px solid #ddd;
            border-radius: 6px;
            font-size: 14px;
        }
        input:focus {
            outline: none;
            border-color: #2196F3;
            box-shadow: 0 0 0 2px rgba(33,150,243,0.1);
        }
        button { 
            padding: 12px 24px;
            background-color: #2196F3;
            color: white;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            font-size: 16px;
            transition: background-color 0.3s;
        }
        button:hover { 
            background-color: #1976D2;
        }
        .switch {
            position: relative;
            display: inline-block;
            width: 60px;
            height: 34px;
            margin: 10px 0;
        }
        .switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }
        .slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: #ccc;
            transition: .4s;
            border-radius: 34px;
        }
        .slider:before {
            position: absolute;
            content: "";
            height: 26px;
            width: 26px;
            left: 4px;
            bottom: 4px;
            background-color: white;
            transition: .4s;
            border-radius: 50%;
        }
        input:checked + .slider {
            background-color: #2196F3;
        }
        input:checked + .slider:before {
            transform: translateX(26px);
        }
        .value-display {
            font-size: 24px;
            font-weight: bold;
            color: #2196F3;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="tab">
            <button class="tablinks" onclick="openTab(event, 'Monitor')" id="defaultOpen">Monitor</button>
            <button class="tablinks" onclick="openTab(event, 'Config')">Configurações</button>
            <button class="tablinks" onclick="openTab(event, 'Info')">Info</button>
        </div>

        <div id="Monitor" class="tabcontent">
            <div class="grid">
                <div class="card">
                    <h3>Temperatura</h3>
                    <div id="temperatura" class="value-display">--°C</div>
                </div>
                <div class="card">
                    <h3>Umidade Ar</h3>
                    <div id="umidade" class="value-display">--%</div>
                </div>
                <div class="card">
                    <h3>Umidade Solo</h3>
                    <div id="umidadeSolo" class="value-display">--%</div>
                </div>
                <div class="card">
                    <h3>Controles</h3>
                    <p>
                        Modo Automático:
                        <label class="switch">
                            <input type="checkbox" id="modoAutomatico" onchange="toggleModo()">
                            <span class="slider"></span>
                        </label>
                    </p>
                    <p>
                        Relé:
                        <label class="switch">
                            <input type="checkbox" id="releStatus" onchange="toggleRele()">
                            <span class="slider"></span>
                        </label>
                    </p>
                </div>
            </div>
        </div>

          <div id="Config" class="tabcontent">
            <form id="configForm" onsubmit="saveConfig(event)">
                <div class="config-section">
                    <h3>Horário de Irrigação</h3>
                    <div class="form-group">
                        <label>Hora:</label>
                        <input type="number" id="horaInicio" name="horaInicio" min="0" max="23">
                    </div>
                    <div class="form-group">
                        <label>Minuto:</label>
                        <input type="number" id="minutoInicio" name="minutoInicio" min="0" max="59">
                    </div>
                </div>

                <div class="config-section">
                    <h3>Configurações MQTT (Opcional)</h3>
                    <div class="form-group">
                        <label>Servidor:</label>
                        <input type="text" id="mqtt_server" name="mqtt_server">
                    </div>
                    <div class="form-group">
                        <label>Porta:</label>
                        <input type="text" id="mqtt_port" name="mqtt_port">
                    </div>
                    <div class="form-group">
                        <label>Usuário:</label>
                        <input type="text" id="mqtt_user" name="mqtt_user">
                    </div>
                    <div class="form-group">
                        <label>Senha:</label>
                        <input type="password" id="mqtt_password" name="mqtt_password">
                    </div>
                </div>
                
                <div class="form-group">
                    <button type="submit">Salvar</button>
                </div>
            </form>
        </div>

        <div id="Info" class="tabcontent">
            <div class="grid">
                <div class="card">
                    <h3>Hora do Sistema</h3>
                    <div id="system-time" class="value-display">--:--:--</div>
                </div>
                <div class="card">
                    <h3>Rede WiFi</h3>
                    <div id="wifi-name" class="value-display">--</div>
                </div>
                <div class="card">
                    <h3>Força do Sinal</h3>
                    <div id="wifi-strength" class="value-display">-- dBm</div>
                </div>
                <div class="card">
                    <h3>Endereço IP</h3>
                    <div id="ip-address" class="value-display">--</div>
                </div>
                <div class="card">
                    <h3>Temperatura ESP32</h3>
                    <div id="esp-temp" class="value-display">--°C</div>
                </div>
                <div class="card">
                    <h3>Memória Livre</h3>
                    <div id="free-memory" class="value-display">--%</div>
                </div>
            </div>
        </div>
    </div>

    <script>

                function loadConfig() {
            fetch('/getConfig')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('horaInicio').value = data.horaInicio;
                    document.getElementById('minutoInicio').value = data.minutoInicio;
                    document.getElementById('mqtt_server').value = data.mqtt_server;
                    document.getElementById('mqtt_port').value = data.mqtt_port;
                    document.getElementById('mqtt_user').value = data.mqtt_user;
                    document.getElementById('mqtt_password').value = data.mqtt_password;
                });
        }

 function openTab(evt, tabName) {
            var i, tabcontent, tablinks;
            tabcontent = document.getElementsByClassName("tabcontent");
            for (i = 0; i < tabcontent.length; i++) {
                tabcontent[i].style.display = "none";
            }
            tablinks = document.getElementsByClassName("tablinks");
            for (i = 0; i < tablinks.length; i++) {
                tablinks[i].className = tablinks[i].className.replace(" active", "");
            }
            document.getElementById(tabName).style.display = "block";
            evt.currentTarget.className += " active";
            
            if (tabName === 'Config') {
                loadConfig();
            }
        }

        document.getElementById("defaultOpen").click();

        function updateSensorData() {
            fetch('/sensorData')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('temperatura').textContent = data.temperatura + '°C';
                    document.getElementById('umidade').textContent = data.umidade + '%';
                    document.getElementById('umidadeSolo').textContent = data.umidadeSolo + '%';
                    document.getElementById('modoAutomatico').checked = data.modoAutomatico;
                    document.getElementById('releStatus').checked = data.releStatus;
                });
        }

        function toggleModo() {
            const modo = document.getElementById('modoAutomatico').checked;
            fetch('/toggleModo', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({modo: modo})
            });
        }

        function toggleRele() {
            const status = document.getElementById('releStatus').checked;
            fetch('/toggleRele', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({status: status})
            });
        }

          function saveConfig(event) {
            event.preventDefault();
            const formData = new FormData(event.target);
            const data = Object.fromEntries(formData.entries());
            
            fetch('/saveConfig', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(data)
            })
            .then(response => response.json())
            .then(result => {
                if (result.success) {
                    alert('Configurações salvas com sucesso!');
                } else {
                    alert('Erro ao salvar configurações: ' + result.message);
                }
            })
            .catch(error => {
                alert('Erro ao salvar configurações: ' + error);
            });
        }

         // Adicione esta função
        function updateSystemInfo() {
            fetch('/systemInfo')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('system-time').textContent = data.hora;
                    document.getElementById('wifi-name').textContent = data.wifi_ssid;
                    document.getElementById('wifi-strength').textContent = data.wifi_strength + ' dBm';
                    document.getElementById('ip-address').textContent = data.ip;
                    document.getElementById('esp-temp').textContent = data.esp_temp.toFixed(1) + '°C';
                    document.getElementById('free-memory').textContent = 
                        data.heap_percent.toFixed(1) + '% (' + 
                        Math.round(data.heap_free / 1024) + 'KB free)';
                });
        }

        // Modifique o setInterval para incluir a atualização das informações do sistema
        setInterval(() => {
            updateSensorData();
            updateSystemInfo();
        }, 2000);
    </script>
</body>
</html>
)rawliteral";

void saveConfig() {
    preferences.begin("config", false);
    preferences.putString("mqtt_server", config.mqtt_server);
    preferences.putString("mqtt_port", config.mqtt_port);
    preferences.putString("mqtt_user", config.mqtt_user);
    preferences.putString("mqtt_password", config.mqtt_password);
    preferences.putInt("horaInicio", config.horaInicio);
    preferences.putInt("minutoInicio", config.minutoInicio);
    config.modoAutomatico = !config.modoAutomatico;
    saveConfig();
    preferences.end();
}

void loadConfig() {
    preferences.begin("config", true);
    strlcpy(config.mqtt_server, preferences.getString("mqtt_server", "").c_str(), sizeof(config.mqtt_server));
    strlcpy(config.mqtt_port, preferences.getString("mqtt_port", "1883").c_str(), sizeof(config.mqtt_port));
    strlcpy(config.mqtt_user, preferences.getString("mqtt_user", "").c_str(), sizeof(config.mqtt_user));
    strlcpy(config.mqtt_password, preferences.getString("mqtt_password", "").c_str(), sizeof(config.mqtt_password));
    config.horaInicio = preferences.getInt("horaInicio", 5);
    config.minutoInicio = preferences.getInt("minutoInicio", 0);
    config.modoAutomatico = preferences.getBool("modoAuto", true);
    preferences.end();
}


// Funções auxiliares
void lerSensores() {
  state.temperatura = dht.readTemperature();
  state.umidade = dht.readHumidity();

  float soloRawTotal = 0;
  int sensoresValidos = 0;
  const int SOIL_SENSOR_PINS[NUM_SOIL_SENSORS] = { SOIL_SENSOR1, SOIL_SENSOR2 };

  for (int i = 0; i < NUM_SOIL_SENSORS; i++) {
    int soloRaw = analogRead(SOIL_SENSOR_PINS[i]);
    if (soloRaw >= 0 && soloRaw <= 4095) {
      float soloPercentage = map(soloRaw, 4095, 0, 0, 100);
      soloRawTotal += soloPercentage;
      sensoresValidos++;
    }
  }

  state.umidadeSolo = (sensoresValidos > 0) ? (soloRawTotal / sensoresValidos) : 0;
}

void controleRele(bool status) {
  state.relayStatus = status;
  digitalWrite(RELAY_PIN, status);
  if (status) {
    state.tempoIrrigacaoInicio = millis();
  }

  // Publica estado no MQTT se conectado
  if (mqttClient.connected()) {
    mqttClient.publish("irrigation/relay/state", status ? "ON" : "OFF");
  }
}

void verificarAgendamento() {
  if (!config.modoAutomatico) return;

  timeClient.update();
  int horaAtual = timeClient.getHours();
  int minutoAtual = timeClient.getMinutes();

  if (horaAtual == config.horaInicio && minutoAtual == config.minutoInicio) {
    if (!state.relayStatus) {
      controleRele(true);
    }
  }

  // Verifica tempo máximo de irrigação
  if (state.relayStatus && ((millis() - state.tempoIrrigacaoInicio >= DURACAO_MAXIMA_IRRIGACAO) || (state.umidadeSolo >= 80))) {
    controleRele(false);
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado. Reconectando...");
    WiFi.reconnect();
  }
}

// Callbacks MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  if (String(topic) == "irrigation/relay/set") {
    if (!config.modoAutomatico) {
      controleRele(message == "ON");
    }
  } else if (String(topic) == "irrigation/mode/set") {
    config.modoAutomatico = (message == "AUTO");
    preferences.begin("config", false);
    config.modoAutomatico = !config.modoAutomatico;
    saveConfig();
    preferences.end();
  }
}

void reconnectMQTT() {
  if (!mqttClient.connected() && (millis() - state.lastMqttRetry >= MQTT_RETRY_INTERVAL)) {

    state.lastMqttRetry = millis();

    if (mqttClient.connect("ESP32_Irrigation",
                           config.mqtt_user,
                           config.mqtt_password)) {

      mqttClient.subscribe("irrigation/relay/set");
      mqttClient.subscribe("irrigation/mode/set");
    }
  }
}

// Handlers do servidor web
void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

void handleSensorData() {
  StaticJsonDocument<200> doc;
  doc["temperatura"] = state.temperatura;
  doc["umidade"] = state.umidade;
  doc["umidadeSolo"] = state.umidadeSolo;
  doc["modoAutomatico"] = config.modoAutomatico;
  doc["releStatus"] = state.relayStatus;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleToggleModo() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, server.arg("plain"));
    config.modoAutomatico = doc["modo"].as<bool>();

    preferences.begin("config", false);
    config.modoAutomatico = !config.modoAutomatico;
    saveConfig();
    preferences.end();

    server.send(200, "text/plain", "OK");
  }
}

void handleToggleRele() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, server.arg("plain"));

    if (!config.modoAutomatico) {
      controleRele(doc["status"].as<bool>());
    }

    server.send(200, "text/plain", "OK");
  }
}

void handleSaveConfig() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, server.arg("plain"));

    preferences.begin("config", false);

    // Salva configurações MQTT
    strlcpy(config.mqtt_server, doc["mqtt_server"] | "", sizeof(config.mqtt_server));
    strlcpy(config.mqtt_port, doc["mqtt_port"] | "1883", sizeof(config.mqtt_port));
    strlcpy(config.mqtt_user, doc["mqtt_user"] | "", sizeof(config.mqtt_user));
    strlcpy(config.mqtt_password, doc["mqtt_password"] | "", sizeof(config.mqtt_password));

    // Salva configurações de horário
    config.horaInicio = doc["horaInicio"] | 5;
    config.minutoInicio = doc["minutoInicio"] | 0;

    // Salva na memória
    preferences.putString("mqtt_server", config.mqtt_server);
    preferences.putString("mqtt_port", config.mqtt_port);
    preferences.putString("mqtt_user", config.mqtt_user);
    preferences.putString("mqtt_password", config.mqtt_password);
    preferences.putInt("horaInicio", config.horaInicio);
    preferences.putInt("minutoInicio", config.minutoInicio);

    preferences.end();

    // Reconecta MQTT com as novas configurações
    if (strlen(config.mqtt_server) > 0) {
      mqttClient.setServer(config.mqtt_server, atoi(config.mqtt_port));
    }

    server.send(200, "text/plain", "Configurações salvas com sucesso!");
  }
}



void handleSystemInfo() {
  StaticJsonDocument<512> doc;

  // Informações de tempo
  timeClient.update();
  doc["hora"] = timeClient.getFormattedTime();

  // Informações de WiFi
  doc["wifi_ssid"] = WiFi.SSID();
  doc["wifi_strength"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();

  // Informações do sistema
  doc["esp_temp"] = getESPTemp();
  doc["heap_total"] = ESP.getHeapSize();
  doc["heap_free"] = ESP.getFreeHeap();
  doc["heap_percent"] = (ESP.getFreeHeap() * 100.0) / ESP.getHeapSize();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void publicarDadosMQTT() {
  if (!mqttClient.connected()) return;

  StaticJsonDocument<200> doc;
  doc["temperatura"] = state.temperatura;
  doc["umidade"] = state.umidade;
  doc["umidadeSolo"] = state.umidadeSolo;
  doc["modoAutomatico"] = config.modoAutomatico;
  doc["releStatus"] = state.relayStatus;

  String payload;
  serializeJson(doc, payload);
  mqttClient.publish("irrigation/status", payload.c_str());
}

void setup() {
    loadConfig();
  Serial.begin(115200);

  // Configuração dos pins
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BTN_MODE, INPUT_PULLUP);

  // Inicializa sensores
  dht.begin();

  // Carrega configurações salvas
  preferences.begin("config", true);
  strlcpy(config.mqtt_server, preferences.getString("mqtt_server", "").c_str(), sizeof(config.mqtt_server));
  strlcpy(config.mqtt_port, preferences.getString("mqtt_port", "1883").c_str(), sizeof(config.mqtt_port));
  strlcpy(config.mqtt_user, preferences.getString("mqtt_user", "").c_str(), sizeof(config.mqtt_user));
  strlcpy(config.mqtt_password, preferences.getString("mqtt_password", "").c_str(), sizeof(config.mqtt_password));

  config.horaInicio = preferences.getInt("horaInicio", 5);
  config.minutoInicio = preferences.getInt("minutoInicio", 0);
  config.modoAutomatico = preferences.getBool("modoAuto", true);
  preferences.end();

  // Configuração WiFi
  WiFi.mode(WIFI_STA);
  wm.autoConnect("ESP_IRRIGATION_AP", "password123");

  // Configuração MQTT
  if (strlen(config.mqtt_server) > 0) {
    mqttClient.setServer(config.mqtt_server, atoi(config.mqtt_port));
    mqttClient.setCallback(callback);
  }

  // Configuração do servidor web
  server.on("/", handleRoot);
  server.on("/systemInfo", handleSystemInfo);
  server.on("/sensorData", handleSensorData);
  server.on("/toggleModo", HTTP_POST, handleToggleModo);
  server.on("/toggleRele", HTTP_POST, handleToggleRele);
  server.on("/saveConfig", HTTP_POST, handleSaveConfig);
  server.begin();

  // Inicializa NTP
  timeClient.begin();
  timeClient.update();

  // Configuração mDNS
  if (!MDNS.begin("irrigation")) {
    Serial.println("Erro ao iniciar mDNS");
  }
}

void loop() {
  static unsigned long lastSensorRead = 0;
  static unsigned long lastMqttPublish = 0;
  unsigned long currentMillis = millis();

  // Processa requisições web
  server.handleClient();

  // Atualiza MQTT
  if (mqttClient.connected()) {
    mqttClient.loop();
  } else {
    reconnectMQTT();
  }

  // Lê sensores a cada 2 segundos
  if (currentMillis - lastSensorRead >= 2000) {
    lerSensores();
    lastSensorRead = currentMillis;
  }

  // Publica dados MQTT a cada 5 segundos
  if (currentMillis - lastMqttPublish >= 5000) {
    publicarDadosMQTT();
    lastMqttPublish = currentMillis;
  }

  // Verifica botão físico
  if (digitalRead(BTN_MODE) == LOW && !config.modoAutomatico) {
    delay(50);  // Debounce
    if (digitalRead(BTN_MODE) == LOW) {
      controleRele(!state.relayStatus);
      while (digitalRead(BTN_MODE) == LOW)
        ;  // Aguarda soltar o botão
    }
  }

  // Verifica agendamento
  verificarAgendamento();

  checkWiFiConnection();

  // Yield para o ESP
  yield();
}