#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <DHT.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// Definições de pinos
#define DHTPIN 13
#define SOLO_PIN1 12
#define SOLO_PIN2 14
#define RELE_PIN 15
#define BOTAO_PIN 27
#define LED_REDE_PIN 25
#define LED_RELE_PIN 33

// Constantes
#define DHTTYPE DHT11
#define TEMPO_IRRIGACAO 900000 // 15 minutos em milissegundos
#define SOLO_MAXIMO 100
#define SOLO_MINIMO 0
#define LEITURA_SOLO_MAX 4095  // Valor máximo da leitura analógica (seco)
#define LEITURA_SOLO_MIN 1000  // Valor mínimo da leitura analógica (úmido)

// Variáveis globais
bool modoAutomatico = true;
bool releAtivo = false;
bool botaoApertado = false;
unsigned long tempoInicioIrrigacao = 0;
int horaInicio = 6;  // Hora padrão (6:00)
int minutoInicio = 0;

// Objetos
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600); // Fortaleza GMT-3
Preferences preferences;

// Configurações de Wi-Fi - SUBSTITUA COM SUAS INFORMAÇÕES

const char* ssid = "VIVOFIBRA-6969-EXT";
const char* password = "Kx8mrWQByh";

// Estados do LED
enum LEDState {
  LED_OK,
  LED_ERRO_WIFI,
  LED_ERRO_SENSOR,
  LED_CONFIGURANDO
};

LEDState estadoLedRede = LED_CONFIGURANDO;

// Funções de controle do sistema
void piscarLed(int pin, int vezes, int duracaoPisca) {
  for (int i = 0; i < vezes; i++) {
    digitalWrite(pin, HIGH);
    delay(duracaoPisca);
    digitalWrite(pin, LOW);
    delay(duracaoPisca);
  }
}

void atualizarLedRede() {
  switch (estadoLedRede) {
    case LED_OK:
      digitalWrite(LED_REDE_PIN, HIGH);
      break;
    case LED_ERRO_WIFI:
      piscarLed(LED_REDE_PIN, 2, 100);
      break;
    case LED_ERRO_SENSOR:
      piscarLed(LED_REDE_PIN, 3, 100);
      break;
    case LED_CONFIGURANDO:
      piscarLed(LED_REDE_PIN, 1, 500);
      break;
  }
}

void atualizarLedRele() {
  if (releAtivo) {
    digitalWrite(LED_RELE_PIN, HIGH);
  } else {
    digitalWrite(LED_RELE_PIN, LOW);
  }
}

void ligarRele() {
  digitalWrite(RELE_PIN, LOW);  // Lógica invertida - LOW liga
  releAtivo = true;
  tempoInicioIrrigacao = millis();
  atualizarLedRele();
}

void desligarRele() {
  digitalWrite(RELE_PIN, HIGH); // Lógica invertida - HIGH desliga
  releAtivo = false;
  atualizarLedRele();
}

int lerUmidadeSolo() {
  int leitura1 = analogRead(SOLO_PIN1);
  int leitura2 = analogRead(SOLO_PIN2);
  
  // Média das leituras
  int mediaLeitura = (leitura1 + leitura2) / 2;
  
  // Mapear a leitura para um percentual (invertido, pois o sensor é capacitivo)
  int umidadePercentual = map(mediaLeitura, LEITURA_SOLO_MAX, LEITURA_SOLO_MIN, SOLO_MINIMO, SOLO_MAXIMO);
  
  // Limitar entre 0% e 100%
  umidadePercentual = constrain(umidadePercentual, SOLO_MINIMO, SOLO_MAXIMO);
  
  return umidadePercentual;
}

bool verificarHorarioIrrigacao() {
  timeClient.update();
  int horaAtual = timeClient.getHours();
  int minutoAtual = timeClient.getMinutes();
  
  return (horaAtual == horaInicio && minutoAtual == minutoInicio);
}

void carregarConfiguracoes() {
  preferences.begin("irrigacao", false);
  horaInicio = preferences.getInt("horaInicio", 6);
  minutoInicio = preferences.getInt("minutoInicio", 0);
  modoAutomatico = preferences.getBool("modoAuto", true);
  preferences.end();
}

void salvarConfiguracoes() {
  preferences.begin("irrigacao", false);
  preferences.putInt("horaInicio", horaInicio);
  preferences.putInt("minutoInicio", minutoInicio);
  preferences.putBool("modoAuto", modoAutomatico);
  preferences.end();
}

// Handlers para o servidor web
void handleRoot() {
  File file = SPIFFS.open("/index.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "Arquivo não encontrado");
  }
}

void handleCSS() {
  File file = SPIFFS.open("/style.css", "r");
  if (file) {
    server.streamFile(file, "text/css");
    file.close();
  } else {
    server.send(404, "text/plain", "Arquivo não encontrado");
  }
}

void handleJS() {
  File file = SPIFFS.open("/script.js", "r");
  if (file) {
    server.streamFile(file, "application/javascript");
    file.close();
  } else {
    server.send(404, "text/plain", "Arquivo não encontrado");
  }
}

void handleDados() {
  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();
  int umidadeSolo = lerUmidadeSolo();
  
  String json = "{";
  json += "\"temperatura\":" + String(isnan(temperatura) ? 0 : temperatura) + ",";
  json += "\"umidade\":" + String(isnan(umidade) ? 0 : umidade) + ",";
  json += "\"umidadeSolo\":" + String(umidadeSolo) + ",";
  json += "\"releAtivo\":" + String(releAtivo ? "true" : "false") + ",";
  json += "\"modoAutomatico\":" + String(modoAutomatico ? "true" : "false") + ",";
  json += "\"horaInicio\":" + String(horaInicio) + ",";
  json += "\"minutoInicio\":" + String(minutoInicio);
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleControle() {
  // Controle do relé no modo manual
  if (server.hasArg("releManual")) {
    if (!modoAutomatico) {
      String acao = server.arg("releManual");
      if (acao == "ligar") {
        ligarRele();
      } else if (acao == "desligar") {
        desligarRele();
      }
    }
  }
  
  // Alteração do modo
  if (server.hasArg("modo")) {
    String modo = server.arg("modo");
    modoAutomatico = (modo == "automatico");
    salvarConfiguracoes();
  }
  
  // Configuração do horário
  if (server.hasArg("horaInicio") && server.hasArg("minutoInicio")) {
    horaInicio = server.arg("horaInicio").toInt();
    minutoInicio = server.arg("minutoInicio").toInt();
    salvarConfiguracoes();
  }
  
  server.send(200, "text/plain", "OK");
}

void setup() {
  // Iniciando Serial
  Serial.begin(115200);
  
  // Configurando os pinos
  pinMode(RELE_PIN, OUTPUT);
  pinMode(BOTAO_PIN, INPUT_PULLUP);
  pinMode(LED_REDE_PIN, OUTPUT);
  pinMode(LED_RELE_PIN, OUTPUT);
  
  // Desliga o relé inicialmente (lógica invertida)
  desligarRele();
  
  // Inicia o sensor DHT
  dht.begin();
  
  // Inicia o sistema de arquivos SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Erro ao montar SPIFFS");
    estadoLedRede = LED_ERRO_SENSOR;
    return;
  }
  
  // Carregar configurações salvas
  carregarConfiguracoes();
  
  // Conectar ao Wi-Fi
  WiFi.begin(ssid, password);
  
  // Espera pela conexão
  unsigned long inicioConexao = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    
    // Timeout de 20 segundos
    if (millis() - inicioConexao > 20000) {
      Serial.println("Falha ao conectar ao Wi-Fi");
      estadoLedRede = LED_ERRO_WIFI;
      break;
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.print("Conectado ao WiFi. IP: ");
    Serial.println(WiFi.localIP());
    estadoLedRede = LED_OK;
    
    // Inicia o cliente NTP
    timeClient.begin();
    
    // Define rotas para a interface web
    server.on("/", HTTP_GET, handleRoot);
    server.on("/style.css", HTTP_GET, handleCSS);
    server.on("/script.js", HTTP_GET, handleJS);
    server.on("/dados", HTTP_GET, handleDados);
    server.on("/controle", HTTP_POST, handleControle);
    
    // Iniciar servidor web
    server.begin();
  }
  
  // Cria os arquivos necessários no SPIFFS
  criarArquivosWeb();
}

void criarArquivosWeb() {
  if (!SPIFFS.exists("/index.html")) {
    File file = SPIFFS.open("/index.html", "w");
    if (file) {
      file.print(R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Sistema de Irrigação Inteligente</title>
    <link rel="stylesheet" href="style.css">
</head>
<body>
    <div class="container">
        <header>
            <h1>Sistema de Irrigação Inteligente</h1>
        </header>
        
        <div class="sensores">
            <div class="sensor">
                <h2>Temperatura</h2>
                <p id="temperatura">--</p>
                <span class="unidade">°C</span>
            </div>
            <div class="sensor">
                <h2>Umidade do Ar</h2>
                <p id="umidade">--</p>
                <span class="unidade">%</span>
            </div>
        </div>
        
        <div class="solo">
            <h2>Umidade do Solo</h2>
            <div class="medidor">
                <div id="barra-solo" class="barra"></div>
            </div>
            <p id="umidade-solo">--</p>
            <span class="unidade">%</span>
        </div>
        
        <div class="controles">
            <div class="controle">
                <h3>Irrigação</h3>
                <button id="btn-rele" class="btn">Ligar Irrigação</button>
            </div>
            <div class="controle">
                <h3>Modo</h3>
                <button id="btn-modo" class="btn">Automático</button>
            </div>
        </div>
        
        <div class="configuracao">
            <h2>Horário de Irrigação</h2>
            <div class="form-group">
                <label for="hora">Hora:</label>
                <select id="hora"></select>
                
                <label for="minuto">Minuto:</label>
                <select id="minuto"></select>
                
                <button id="btn-salvar" class="btn">Salvar</button>
            </div>
        </div>
        
        <div class="status">
            <div id="status-modo" class="status-item">
                <span class="status-label">Modo:</span>
                <span id="modo-valor">Automático</span>
            </div>
            <div id="status-rele" class="status-item">
                <span class="status-label">Irrigação:</span>
                <span id="rele-valor">Desligada</span>
            </div>
        </div>
    </div>
    
    <script src="script.js"></script>
</body>
</html>
)rawliteral");
      file.close();
    }
  }

  if (!SPIFFS.exists("/style.css")) {
    File file = SPIFFS.open("/style.css", "w");
    if (file) {
      file.print(R"rawliteral(
* {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
    font-family: 'Arial', sans-serif;
}

body {
    background-color: #f5f5f5;
    color: #333;
}

.container {
    max-width: 800px;
    margin: 0 auto;
    padding: 20px;
}

header {
    background-color: #2c3e50;
    color: white;
    padding: 15px;
    text-align: center;
    border-radius: 8px;
    margin-bottom: 20px;
}

.sensores {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 20px;
    margin-bottom: 20px;
}

.sensor {
    background-color: white;
    border-radius: 8px;
    padding: 15px;
    text-align: center;
    box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
}

.sensor h2 {
    margin-bottom: 10px;
    font-size: 1.2em;
    color: #2c3e50;
}

.sensor p {
    font-size: 2em;
    font-weight: bold;
    color: #3498db;
}

.solo {
    background-color: white;
    border-radius: 8px;
    padding: 15px;
    text-align: center;
    margin-bottom: 20px;
    box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
}

.solo h2 {
    margin-bottom: 10px;
    font-size: 1.2em;
    color: #2c3e50;
}

.medidor {
    background-color: #ecf0f1;
    height: 20px;
    border-radius: 10px;
    margin-bottom: 10px;
    overflow: hidden;
}

.barra {
    height: 100%;
    width: 0%;
    background-color: #27ae60;
    transition: width 0.5s ease;
}

.controles {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 20px;
    margin-bottom: 20px;
}

.controle {
    background-color: white;
    border-radius: 8px;
    padding: 15px;
    text-align: center;
    box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
}

.controle h3 {
    margin-bottom: 10px;
    color: #2c3e50;
}

.btn {
    background-color: #3498db;
    color: white;
    border: none;
    padding: 10px 15px;
    border-radius: 5px;
    cursor: pointer;
    font-weight: bold;
    transition: background-color 0.3s;
}

.btn:hover {
    background-color: #2980b9;
}

.configuracao {
    background-color: white;
    border-radius: 8px;
    padding: 15px;
    margin-bottom: 20px;
    box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
}

.configuracao h2 {
    margin-bottom: 15px;
    font-size: 1.2em;
    color: #2c3e50;
    text-align: center;
}

.form-group {
    display: flex;
    flex-wrap: wrap;
    justify-content: center;
    align-items: center;
    gap: 10px;
}

.form-group label {
    font-weight: bold;
}

.form-group select {
    padding: 8px;
    border-radius: 5px;
    border: 1px solid #ddd;
}

.status {
    background-color: white;
    border-radius: 8px;
    padding: 15px;
    display: flex;
    justify-content: space-around;
    box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
}

.status-item {
    text-align: center;
}

.status-label {
    font-weight: bold;
    margin-right: 5px;
}

/* Estilos especiais para botões */
#btn-rele.ativo {
    background-color: #e74c3c;
}

#btn-modo.manual {
    background-color: #f39c12;
}

/* Estilos para dispositivos móveis */
@media (max-width: 600px) {
    .sensores, .controles {
        grid-template-columns: 1fr;
    }

    .form-group {
        flex-direction: column;
        align-items: stretch;
    }

    .status {
        flex-direction: column;
        gap: 10px;
    }
}

.unidade {
    font-size: 0.8em;
    color: #7f8c8d;
}

#umidade-solo {
    font-size: 2em;
    font-weight: bold;
    color: #27ae60;
    display: inline-block;
}
)rawliteral");
      file.close();
    }
  }

  if (!SPIFFS.exists("/script.js")) {
    File file = SPIFFS.open("/script.js", "w");
    if (file) {
      file.print(R"rawliteral(
document.addEventListener('DOMContentLoaded', function() {
    // Elementos da interface
    const temperaturaEl = document.getElementById('temperatura');
    const umidadeEl = document.getElementById('umidade');
    const umidadeSoloEl = document.getElementById('umidade-solo');
    const barraSoloEl = document.getElementById('barra-solo');
    const btnReleEl = document.getElementById('btn-rele');
    const btnModoEl = document.getElementById('btn-modo');
    const horaEl = document.getElementById('hora');
    const minutoEl = document.getElementById('minuto');
    const btnSalvarEl = document.getElementById('btn-salvar');
    const modoValorEl = document.getElementById('modo-valor');
    const releValorEl = document.getElementById('rele-valor');
    
    // Preencher as opções de hora e minuto
    for (let i = 0; i < 24; i++) {
        const option = document.createElement('option');
        option.value = i;
        option.text = i.toString().padStart(2, '0');
        horaEl.appendChild(option);
    }
    
    for (let i = 0; i < 60; i++) {
        const option = document.createElement('option');
        option.value = i;
        option.text = i.toString().padStart(2, '0');
        minutoEl.appendChild(option);
    }
    
    // Estado local
    let modoAutomatico = true;
    let releAtivo = false;
    
    // Função para atualizar dados
    function atualizarDados() {
        fetch('/dados')
            .then(response => response.json())
            .then(data => {
                temperaturaEl.textContent = data.temperatura.toFixed(1);
                umidadeEl.textContent = data.umidade.toFixed(1);
                umidadeSoloEl.textContent = data.umidadeSolo;
                barraSoloEl.style.width = `${data.umidadeSolo}%`;
                
                // Atualiza estado do relé
                releAtivo = data.releAtivo;
                if (releAtivo) {
                    btnReleEl.textContent = 'Desligar Irrigação';
                    btnReleEl.classList.add('ativo');
                    releValorEl.textContent = 'Ligada';
                } else {
                    btnReleEl.textContent = 'Ligar Irrigação';
                    btnReleEl.classList.remove('ativo');
                    releValorEl.textContent = 'Desligada';
                }
                
                // Atualiza modo
                modoAutomatico = data.modoAutomatico;
                if (modoAutomatico) {
                    btnModoEl.textContent = 'Automático';
                    btnModoEl.classList.remove('manual');
                    modoValorEl.textContent = 'Automático';
                } else {
                    btnModoEl.textContent = 'Manual';
                    btnModoEl.classList.add('manual');
                    modoValorEl.textContent = 'Manual';
                }
                
                // Atualiza formulário de horário
                horaEl.value = data.horaInicio;
                minutoEl.value = data.minutoInicio;
                
                // Atualiza cor da barra de acordo com o nível
                if (data.umidadeSolo < 30) {
                    barraSoloEl.style.backgroundColor = '#e74c3c'; // Vermelho - seco
                } else if (data.umidadeSolo < 70) {
                    barraSoloEl.style.backgroundColor = '#f39c12'; // Laranja - médio
                } else {
                    barraSoloEl.style.backgroundColor = '#27ae60'; // Verde - úmido
                }
            })
            .catch(error => console.error('Erro ao obter dados:', error));
    }
    
    // Eventos de botões
    btnReleEl.addEventListener('click', function() {
        if (modoAutomatico) {
            alert('Desative o modo automático para controlar manualmente.');
            return;
        }
        
        const acao = releAtivo ? 'desligar' : 'ligar';
        
        fetch('/controle', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: `releManual=${acao}`
        })
        .then(response => {
            if (response.ok) {
                // Atualiza imediatamente para feedback visual rápido
                releAtivo = !releAtivo;
                if (releAtivo) {
                    btnReleEl.textContent = 'Desligar Irrigação';
                    btnReleEl.classList.add('ativo');
                    releValorEl.textContent = 'Ligada';
                } else {
                    btnReleEl.textContent = 'Ligar Irrigação';
                    btnReleEl.classList.remove('ativo');
                    releValorEl.textContent = 'Desligada';
                }
                setTimeout(atualizarDados, 500); // Atualiza dados após breve pausa
            }
        })
        .catch(error => console.error('Erro ao controlar relé:', error));
    });
    
    btnModoEl.addEventListener('click', function() {
        const novoModo = modoAutomatico ? 'manual' : 'automatico';
        
        fetch('/controle', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: `modo=${novoModo}`
        })
        .then(response => {
            if (response.ok) {
                // Atualiza imediatamente para feedback visual rápido
                modoAutomatico = !modoAutomatico;
                if (modoAutomatico) {
                    btnModoEl.textContent = 'Automático';
                    btnModoEl.classList.remove('manual');
                    modoValorEl.textContent = 'Automático';
                } else {
                    btnModoEl.textContent = 'Manual';
                    btnModoEl.classList.add('manual');
                    modoValorEl.textContent = 'Manual';
                }
                setTimeout(atualizarDados, 500); // Atualiza dados após breve pausa
            }
        })
        .catch(error => console.error('Erro ao alterar modo:', error));
    });
    
    btnSalvarEl.addEventListener('click', function() {
        const hora = horaEl.value;
        const minuto = minutoEl.value;
        
        fetch('/controle', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: `horaInicio=${hora}&minutoInicio=${minuto}`
        })
        .then(response => {
            if (response.ok) {
                alert('Horário de irrigação atualizado com sucesso!');
                setTimeout(atualizarDados, 500); // Atualiza dados após breve pausa
            }
        })
        .catch(error => console.error('Erro ao salvar horário:', error));
    });
    
    // Atualizar dados a cada 2 segundos
    atualizarDados();
    setInterval(atualizarDados, 2000);
});
)rawliteral");
      file.close();
    }
  }
}

void verificarBotao() {
  static unsigned long ultimaPressao = 0;
  static bool ultimoEstadoBotao = HIGH; // Pullup, botão não pressionado = HIGH
  
  bool estadoAtual = digitalRead(BOTAO_PIN);
  
  // Debounce e detecção de borda
  if (estadoAtual != ultimoEstadoBotao && millis() - ultimaPressao > 50) {
    ultimaPressao = millis();
    
    // Botão foi pressionado (LOW devido ao pullup)
    if (estadoAtual == LOW) {
      if (!modoAutomatico) {
        // Alternar estado do relé no modo manual
        if (releAtivo) {
          desligarRele();
        } else {
          ligarRele();
        }
      } else {
        // No modo automático, pressionar o botão por mais tempo muda o modo
        botaoApertado = true;
      }
    } else {
      botaoApertado = false;
    }
    
    ultimoEstadoBotao = estadoAtual;
  }
  
  // Se o botão estiver pressionado por mais de 3 segundos, muda o modo
  static unsigned long inicioPress = 0;
  if (botaoApertado && digitalRead(BOTAO_PIN) == LOW) {
    if (inicioPress == 0) {
      inicioPress = millis();
    } else if (millis() - inicioPress > 3000) {
      modoAutomatico = !modoAutomatico;
      botaoApertado = false;
      inicioPress = 0;
      
      // Piscar LED para confirmar mudança de modo
      piscarLed(LED_REDE_PIN, modoAutomatico ? 3 : 2, 200);
      
      // Salvar a configuração
      salvarConfiguracoes();
    }
  } else {
    inicioPress = 0;
  }
}

void loop() {
  // Lidar com clientes HTTP
  server.handleClient();
  
  // Atualizar estado do LED de rede
  atualizarLedRede();
  
  // Verificar botão físico
  verificarBotao();
  
  // Lógica do sistema de irrigação
  if (modoAutomatico) {
    // Verificar se é hora de iniciar a irrigação
    if (!releAtivo && verificarHorarioIrrigacao()) {
      ligarRele();
    }
    
    // Verificar condições para desligar a irrigação
    if (releAtivo) {
      // Verifica se o solo está suficientemente úmido
      if (lerUmidadeSolo() >= SOLO_MAXIMO) {
        desligarRele();
      }
      
      // Verifica se o tempo máximo de irrigação foi atingido
      if (millis() - tempoInicioIrrigacao > TEMPO_IRRIGACAO) {
        desligarRele();
      }
    }
  }
  
  // Pequena pausa para estabilidade
  delay(100);
}
