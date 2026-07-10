#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <SPI.h>
#include "AudioFileSourceHTTPStream.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// --- CONFIGURAÇÕES DE REDE ---
const char* ssid = "NOME_DA_REDE";
const char* password = "SENHA_DA_REDE";

// --- MAPEAMENTO DE PINOS ---
// Botões
const int BTN_1 = 32;
const int BTN_2 = 33;
const int BTN_3 = 25;

// Display (JLX19296G-770)
// ==============================================
// Display -> ESP32
// D0 -> D18
// D1 -> D23
// D2 -> D23
// D3 -> D23
// RS -> D17
// RST -> D16
// CS -> D5
// LEDA -> D4
// ==============================================
#define PIN_CLK   18
#define PIN_MOSI  23
#define PIN_CS     5
#define PIN_DC    17
#define PIN_RES   16
#define PIN_LEDA   4  

// --- OBJETOS ---
U8G2_ST75256_JLX19296_F_4W_SW_SPI u8g2(U8G2_R0, PIN_CLK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RES);

AudioGeneratorMP3 *mp3;
AudioFileSourceHTTPStream *file;
AudioOutputI2S *out;

// --- VARIÁVEIS GLOBAIS ---
unsigned long ultimoAperto = 0;
const unsigned long DEBOUNCE_DELAY = 500; 

const int pwmFreq = 5000;
const int pwmResolution = 8; 

// Variáveis de Animação
unsigned long tempoUltimoPiscar = 0;
unsigned long intervaloPiscar = 3000; 
unsigned long tempoAnimacaoFala = 0;
bool estaPiscando = false;
bool bocaAberta = false;
String emocaoAtual = "neutro"; 

// Declaração de funções
void desenharRosto(String emocao);
void enviarMensagem(String mensagem);

void setup() {
  Serial.begin(115200);

  // 1. Configuração dos Botões
  pinMode(BTN_1, INPUT_PULLUP);
  pinMode(BTN_2, INPUT_PULLUP);
  pinMode(BTN_3, INPUT_PULLUP);

  // 2. Configuração do Brilho
  ledcAttach(PIN_LEDA, pwmFreq, pwmResolution);
  ledcWrite(PIN_LEDA, 255); 

  // 3. Inicialização do Display
  u8g2.begin();
  u8g2.setContrast(130); // COLOQUE SEU VALOR CALIBRADO AQUI
  desenharRosto("neutro");

  // 4. Inicialização da Rede
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  Serial.println("\nWiFi Conectado!");

  // 5. Inicialização do Áudio
  out = new AudioOutputI2S();
  // Pinos I2S: BCLK (26), LRC (27), DIN (22)
  out->SetPinout(26, 27, 22); 
  mp3 = new AudioGeneratorMP3();
}

void loop() {
  // --- LÓGICA DE ÁUDIO E ANIMAÇÃO DE FALA ---
  if (mp3 && mp3->isRunning()) {
    if (!mp3->loop()) {
      mp3->stop();
      Serial.println("Áudio finalizado.");
      emocaoAtual = "neutro";
      desenharRosto("neutro");
      
      // Limpa os ponteiros de memória para evitar vazamento
      if (file) { delete file; file = nullptr; }
    } else {
      // BMO move a boca enquanto o áudio toca (atualiza a cada 150ms para não travar o buffer do MP3)
      if (millis() - tempoAnimacaoFala > 150) {
        bocaAberta = !bocaAberta;
        desenharRosto(bocaAberta ? "falando_aberta" : "falando_fechada");
        tempoAnimacaoFala = millis();
      }
    }
  } 
  else {
    // --- LÓGICA DOS BOTÕES ---
    if (millis() - ultimoAperto > DEBOUNCE_DELAY) {
      if (digitalRead(BTN_1) == LOW) {
        enviarMensagem("BMO, me conte uma piada curta sobre eletrônica.");
        ultimoAperto = millis();
      } 
      else if (digitalRead(BTN_2) == LOW) {
        enviarMensagem("BMO, imite um robô com defeito.");
        ultimoAperto = millis();
      }
      else if (digitalRead(BTN_3) == LOW) {
        enviarMensagem("BMO, qual o sentido da vida para um computador?");
        ultimoAperto = millis();
      }
    }

    // --- LÓGICA DE PISCAR OS OLHOS (Apenas quando não está falando ou processando) ---
    if (emocaoAtual == "neutro") {
      if (!estaPiscando && (millis() - tempoUltimoPiscar > intervaloPiscar)) {
        estaPiscando = true;
        tempoUltimoPiscar = millis();
        desenharRosto("piscando");
      } 
      else if (estaPiscando && (millis() - tempoUltimoPiscar > 150)) {
        estaPiscando = false;
        tempoUltimoPiscar = millis();
        intervaloPiscar = random(2000, 6000); 
        desenharRosto("neutro");
      }
    }
  }
}

void enviarMensagem(String mensagem) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  desenharRosto("feliz"); 
  Serial.println("\nAbrindo socket TCP direto...");
  
  WiFiClient client;
  if (!client.connect("IP_DO_SERVIDOR", PORTA)) {
    Serial.println("Falha na conexão TCP.");
    desenharRosto("triste");
    delay(3000);
    desenharRosto("neutro");
    return;
  }

  StaticJsonDocument<256> doc;
  doc["mensagem"] = mensagem;
  String requestBody;
  serializeJson(doc, requestBody);

  Serial.println("Enviando pacote HTTP manual e aguardando IA (Timeout de 30s)...");

  client.println("POST /api/bmo HTTP/1.1");
  client.println("Host: xxx.xxx.xx.xx");
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(requestBody.length());
  client.println("Connection: close");
  client.println(); 
  client.print(requestBody);

  unsigned long timeout = millis();
  while (client.connected() && !client.available()) {
    if (millis() - timeout > 30000) { // O seu timeout cirúrgico ajustado para 30s
      Serial.println("Timeout aguardando a API!");
      client.stop();
      desenharRosto("triste");
      delay(3000);
      desenharRosto("neutro");
      return;
    }
    delay(10);
  }

  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") { break; }
  }

  String payload = client.readString();
  StaticJsonDocument<512> responseDoc;
  DeserializationError error = deserializeJson(responseDoc, payload);
  
  if (!error) {
    const char* audioUrl = responseDoc["audio_url"];
    Serial.print("Áudio recebido! Baixando: ");
    Serial.println(audioUrl);

    // Inicia o download e a reprodução
    // A animação da boca vai começar automaticamente lá no loop()
    file = new AudioFileSourceHTTPStream(audioUrl);
    mp3->begin(file, out);
    
    // Altera o estado para pular a lógica de botões enquanto fala
    emocaoAtual = "falando"; 
    
  } else {
    Serial.println("Erro ao decodificar JSON.");
    desenharRosto("triste");
    delay(3000);
    desenharRosto("neutro");
  }
  
  client.stop(); 
}

void desenharRosto(String emocao) {
  if (emocao != "piscando" && !emocao.startsWith("falando")) {
    emocaoAtual = emocao;
  }

  u8g2.clearBuffer(); 
  
  if (emocao == "feliz") {
    u8g2.drawBox(76, 30, 8, 8);  
    u8g2.drawBox(108, 30, 8, 8); 
    u8g2.drawCircle(96, 50, 20, U8G2_DRAW_LOWER_RIGHT | U8G2_DRAW_LOWER_LEFT); 
  } 
  else if (emocao == "triste") {
    u8g2.drawBox(76, 35, 8, 8);  
    u8g2.drawBox(108, 35, 8, 8); 
    u8g2.drawCircle(96, 65, 15, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT); 
  } 
  else if (emocao == "piscando") {
    u8g2.drawLine(76, 39, 84, 39); 
    u8g2.drawLine(108, 39, 116, 39);
    u8g2.drawLine(85, 60, 107, 60); 
  }
  else if (emocao == "falando_aberta") {
    u8g2.drawBox(76, 35, 8, 8);  
    u8g2.drawBox(108, 35, 8, 8); 
    u8g2.drawBox(86, 55, 20, 15); // Boca bem aberta
  }
  else if (emocao == "falando_fechada") {
    u8g2.drawBox(76, 35, 8, 8);  
    u8g2.drawBox(108, 35, 8, 8); 
    u8g2.drawLine(85, 60, 107, 60); // Boca fechada
  }
  else {
    // Neutro
    u8g2.drawBox(76, 35, 8, 8);  
    u8g2.drawBox(108, 35, 8, 8); 
    u8g2.drawLine(85, 60, 107, 60); 
  }
  
  u8g2.sendBuffer(); 
}
