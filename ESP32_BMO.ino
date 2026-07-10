#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <SPI.h>

// --- CONFIGURAÇÕES DE REDE ---
const char* ssid = "NOME_DA_REDE";
const char* password = "SENHA_DA_REDE";
const char* serverUrl = "http://xxx.xxx.xxx.x:xxxx/api";

// --- MAPEAMENTO DE PINOS ---
// Botões
const int BTN_1 = 32;
const int BTN_2 = 33;
const int BTN_3 = 25;

// Display (Conforme sua pinagem serial da placa JLX)
#define PIN_CLK   18
#define PIN_MOSI  23
#define PIN_CS     5
#define PIN_DC    17
#define PIN_RES   16
#define PIN_LEDA   4  // Pino do Backlight (PWM)

// --- OBJETOS ---
U8G2_ST75256_JLX19296_F_4W_SW_SPI u8g2(U8G2_R0, PIN_CLK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RES);

// --- VARIÁVEIS GLOBAIS ---
// Controle dos Botões
unsigned long ultimoAperto = 0;
const unsigned long DEBOUNCE_DELAY = 500; 

// Configurações do PWM do Backlight (LEDA)
const int pwmFreq = 5000;
const int pwmResolution = 8; // 0 a 255

// Variáveis para a animação dos olhos
unsigned long tempoUltimoPiscar = 0;
unsigned long intervaloPiscar = 3000; // Tempo até a próxima piscada
bool estaPiscando = false;
String emocaoAtual = "neutro"; // Guarda o estado do rosto

// Declaração de funções
void desenharRosto(String emocao);
void enviarMensagem(String mensagem);

void setup() {
  Serial.begin(115200);

  // 1. Configura os Botões
  pinMode(BTN_1, INPUT_PULLUP);
  pinMode(BTN_2, INPUT_PULLUP);
  pinMode(BTN_3, INPUT_PULLUP);

  // 2. Configura o Brilho (LEDA) via PWM para ESP32 Core 3.0+
  ledcAttach(PIN_LEDA, pwmFreq, pwmResolution);
  ledcWrite(PIN_LEDA, 255); // Ajuste o brilho aqui (0 a 255)

  // 3. Inicializa o Display e Ajusta Contraste
  u8g2.begin();
  
  // ---> COLOQUE O SEU NÚMERO MÁGICO DE CONTRASTE AQUI:
  u8g2.setContrast(130); 
  
  desenharRosto("neutro");
  Serial.println("Display inicializado. Rosto Neutro ativado.");

  // 4. Conexão WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  Serial.println("\nWiFi Conectado com sucesso!");
}

void loop() {
  // --- LÓGICA DOS BOTÕES ---
  if (millis() - ultimoAperto > DEBOUNCE_DELAY) {
    if (digitalRead(BTN_1) == LOW) {
      Serial.println("\nBotão 1 pressionado!");
      enviarMensagem("BMO, me conte uma piada curta sobre videogames.");
      ultimoAperto = millis();
    } 
    else if (digitalRead(BTN_2) == LOW) {
      Serial.println("\nBotão 2 pressionado!");
      enviarMensagem("BMO, imite um robô com defeito.");
      ultimoAperto = millis();
    }
    else if (digitalRead(BTN_3) == LOW) {
      Serial.println("\nBotão 3 pressionado!");
      enviarMensagem("BMO, qual o sentido da vida para um computador?");
      ultimoAperto = millis();
    }
  }

  // --- LÓGICA DE ANIMAÇÃO (PISCAR) ---
  if (emocaoAtual == "neutro") {
    // Hora de fechar os olhos?
    if (!estaPiscando && (millis() - tempoUltimoPiscar > intervaloPiscar)) {
      estaPiscando = true;
      tempoUltimoPiscar = millis();
      desenharRosto("piscando");
    } 
    // Hora de abrir os olhos? (Deixa fechado por 150ms)
    else if (estaPiscando && (millis() - tempoUltimoPiscar > 150)) {
      estaPiscando = false;
      tempoUltimoPiscar = millis();
      intervaloPiscar = random(2000, 6000); // Sorteia o próximo pisco entre 2s e 6s
      desenharRosto("neutro");
    }
  }
}

void enviarMensagem(String mensagem) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Erro: WiFi desconectado!");
    return;
  }
  
  desenharRosto("feliz"); // Fica com cara de quem está "pensando/escutando"
  Serial.println("Enviando requisição para a API Docker...");
  
  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  // Prepara o JSON para envio
  StaticJsonDocument<256> doc;
  doc["mensagem"] = mensagem;
  String requestBody;
  serializeJson(doc, requestBody);

  // Dispara o POST e aguarda a API processar e gerar o áudio
  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode == 200) {
    String payload = http.getString();
    
    // Decodifica a resposta da sua API
    StaticJsonDocument<512> responseDoc;
    deserializeJson(responseDoc, payload);
    const char* audioUrl = responseDoc["audio_url"];
    
    Serial.println("--- SUCESSO ---");
    Serial.print("URL do Áudio Gerado: ");
    Serial.println(audioUrl);
    Serial.println("-----------------");

    // Mantém a carinha feliz por 3 segundos simulando o tempo da fala
    delay(3000); 
    desenharRosto("neutro");
    
  } else {
    Serial.print("Falha na API. Código HTTP: ");
    Serial.println(httpResponseCode);
    
    // Expressão de erro
    desenharRosto("triste"); 
    delay(3000);
    desenharRosto("neutro");
  }
  
  http.end();
}

void desenharRosto(String emocao) {
  // Salva o estado atual, exceto se for uma animação transitória
  if (emocao != "piscando") {
    emocaoAtual = emocao;
  }

  u8g2.clearBuffer(); 
  
  if (emocao == "feliz") {
    // Olhos quadrados e sorriso
    u8g2.drawBox(76, 30, 8, 8);  
    u8g2.drawBox(108, 30, 8, 8); 
    u8g2.drawCircle(96, 50, 20, U8G2_DRAW_LOWER_RIGHT | U8G2_DRAW_LOWER_LEFT); 
  } 
  else if (emocao == "triste") {
    // Olhos e boca triste (curva invertida mais abaixo)
    u8g2.drawBox(76, 35, 8, 8);  
    u8g2.drawBox(108, 35, 8, 8); 
    u8g2.drawCircle(96, 65, 15, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT); 
  } 
  else if (emocao == "piscando") {
    // Desenha uma linha fina no lugar do quadrado do olho
    u8g2.drawLine(76, 39, 84, 39); 
    u8g2.drawLine(108, 39, 116, 39);
    u8g2.drawLine(85, 60, 107, 60); // Boca neutra
  }
  else {
    // Rosto neutro / Aguardando
    u8g2.drawBox(76, 35, 8, 8);  
    u8g2.drawBox(108, 35, 8, 8); 
    u8g2.drawLine(85, 60, 107, 60); 
  }
  
  u8g2.sendBuffer(); 
}