/*Utilizar Board NodeMCU-32S*/
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <SPI.h>
#include "AudioFileSourceHTTPStream.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include <driver/i2s.h> // Adicionado para controle do Microfone

// --- CONFIGURAÇÕES DE REDE ---
const char* ssid = "AP 801";
const char* password = "ENGHOUSE";

// --- MAPEAMENTO DE PINOS ---
const int BTN_1 = 32; // Agora será usado como Push-to-Talk
const int BTN_2 = 33;
const int BTN_3 = 25;

// Pinos Display
#define PIN_CLK   18
#define PIN_MOSI  23
#define PIN_CS     5
#define PIN_DC    17
#define PIN_RES   16
#define PIN_LEDA   4  

// Pinos Microfone INMP441 (I2S_NUM_1)
#define I2S_MIC_SCK 14
#define I2S_MIC_WS  15
#define I2S_MIC_SD  13

// --- OBJETOS ---
U8G2_ST75256_JLX19296_F_4W_SW_SPI u8g2(U8G2_R0, PIN_CLK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RES);

AudioGeneratorMP3 *mp3;
AudioFileSourceHTTPStream *file;
AudioOutputI2S *out;

// --- MÁQUINA DE ESTADOS E ANIMAÇÃO ---
enum BMOState { IDLE, LISTENING, THINKING, SPEAKING };
BMOState estadoAtual = IDLE;

int frameAtual = 0;
unsigned long tempoUltimoFrame = 0;
unsigned long intervaloPiscar = 3000; 

const int pwmFreq = 5000;
const int pwmResolution = 8; 

// --- VARIÁVEIS DE GRAVAÇÃO (MIC) ---
const int record_time = 4; // Tempo máximo de gravação em segundos para não estourar a RAM
const int headerSize = 44;
const int byteRate = 16000 * 2; // 16kHz, 16-bit mono
const int wavDataSize = record_time * byteRate;
uint8_t* audioBuffer = nullptr; 
bool isRecording = false;

// Declaração de funções
void mudarEstado(BMOState novoEstado);
void atualizarAnimacao();
void desenharRostoState();
void enviarMensagem(String mensagem);
void enviarAudioParaIA();
void configurarMicrofone();
void gravarAudio();

void setup() {
  Serial.begin(115200);

  pinMode(BTN_1, INPUT_PULLUP);
  pinMode(BTN_2, INPUT_PULLUP);
  pinMode(BTN_3, INPUT_PULLUP);

  ledcAttach(PIN_LEDA, pwmFreq, pwmResolution);
  ledcWrite(PIN_LEDA, 255); 

  u8g2.begin();
  u8g2.setContrast(130);
  mudarEstado(IDLE);

  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  Serial.println("\nWiFi Conectado!");

  // Inicialização do Áudio OUT (I2S_NUM_0 por baixo dos panos)
  out = new AudioOutputI2S();
  out->SetPinout(26, 27, 22); 
  mp3 = new AudioGeneratorMP3();

  // Inicialização do Microfone (I2S_NUM_1)
  configurarMicrofone();
}

void loop() {
  if (mp3 && mp3->isRunning()) {
    if (!mp3->loop()) {
      mp3->stop();
      Serial.println("Áudio finalizado.");
      mudarEstado(IDLE);
      if (file) { delete file; file = nullptr; }
    } else {
      atualizarAnimacao(); 
    }
  } 
  else {
    atualizarAnimacao(); 

    // Lógica do Push-to-Talk (Segurar BTN_1 para gravar)
    if (digitalRead(BTN_1) == LOW && !isRecording) {
      Serial.println("Botão pressionado. Iniciando gravação...");
      gravarAudio();
    }
    else if (digitalRead(BTN_2) == LOW) {
      delay(300); // Debounce simples
      enviarMensagem("BMO, imite um robô com defeito.");
    }
    else if (digitalRead(BTN_3) == LOW) {
      delay(300);
      enviarMensagem("BMO, qual o sentido da vida para um computador?");
    }
  }
}

// --- CONFIGURAÇÃO E GRAVAÇÃO DO MICROFONE ---

void configurarMicrofone() {
  i2s_config_t i2s_mic_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t i2s_mic_pins = {
    .bck_io_num = I2S_MIC_SCK,
    .ws_io_num = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD
  };

  i2s_driver_install(I2S_NUM_1, &i2s_mic_config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &i2s_mic_pins);
}

void gravarAudio() {
  isRecording = true;
  mudarEstado(LISTENING);

  // Aloca buffer na RAM (aprox 128KB para 4 segundos)
  audioBuffer = (uint8_t*) ps_malloc(wavDataSize); 
  if (audioBuffer == nullptr) {
    audioBuffer = (uint8_t*) malloc(wavDataSize); // Tenta na RAM interna se não tiver PSRAM
  }

  if (audioBuffer == nullptr) {
    Serial.println("Erro: Memória insuficiente para gravar áudio.");
    mudarEstado(IDLE);
    isRecording = false;
    return;
  }

  Serial.println("Gravando...");
  size_t bytesLeitura = 0;
  int offset = 0;

  // Grava enquanto o botão estiver pressionado OU até o limite de tempo/tamanho
  while (digitalRead(BTN_1) == LOW && offset < wavDataSize) {
    i2s_read(I2S_NUM_1, &audioBuffer[offset], 1024, &bytesLeitura, portMAX_DELAY);
    offset += bytesLeitura;
    atualizarAnimacao(); // Mantém o BMO animado enquanto ouve
    delay(1);
  }

  Serial.printf("Gravação finalizada. Bytes gravados: %d\n", offset);
  isRecording = false;
  
  if (offset > 16000) { // Envia apenas se tiver gravado pelo menos meio segundo
    enviarAudioParaIA(offset);
  } else {
    mudarEstado(IDLE);
  }

  // Libera a memória após o envio
  free(audioBuffer);
  audioBuffer = nullptr;
}

// --- ENVIO DE ÁUDIO PARA A API ---

void enviarAudioParaIA(int tamanhoAudio) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  mudarEstado(THINKING); 
  Serial.println("\nConectando à IA para enviar áudio...");
  
  WiFiClient client;
  // Aumentei o timeout do socket para lidar com STT
  client.setTimeout(45000); 

  if (!client.connect("192.168.10.5", 8000)) {
    Serial.println("Falha na conexão TCP.");
    mudarEstado(IDLE);
    return;
  }

  Serial.println("Enviando pacote de áudio (Raw PCM 16kHz)...");

  // ATENÇÃO: Endpoint alterado para /api/bmo/voice (Ajuste no seu backend)
  client.println("POST /api/bmo/voice HTTP/1.1");
  client.println("Host: 192.168.10.5");
  client.println("Content-Type: application/octet-stream"); // Indicando envio binário
  client.print("Content-Length: ");
  client.println(tamanhoAudio);
  client.println("Connection: close");
  client.println(); 

  // Envia os bytes gravados em blocos (chunking manual)
  int chunkSize = 2048;
  for (int i = 0; i < tamanhoAudio; i += chunkSize) {
    int bytesToSend = min(chunkSize, tamanhoAudio - i);
    client.write(&audioBuffer[i], bytesToSend);
    atualizarAnimacao(); // Continua "pensando" durante o upload
  }

  // Aguarda a resposta (mesma lógica anterior)
  unsigned long timeout = millis();
  while (client.connected() && !client.available()) {
    if (millis() - timeout > 60000) { 
      Serial.println("Timeout aguardando processamento da voz!");
      client.stop();
      mudarEstado(IDLE);
      return;
    }
    atualizarAnimacao(); 
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

    file = new AudioFileSourceHTTPStream(audioUrl);
    mp3->begin(file, out);
    mudarEstado(SPEAKING); 
  } else {
    Serial.println("Erro ao decodificar JSON da resposta da voz.");
    mudarEstado(IDLE);
  }
  
  client.stop(); 
}

// --- GERENCIAMENTO DE ESTADOS E FRAMES ---

void mudarEstado(BMOState novoEstado) {
  if (estadoAtual != novoEstado) {
    estadoAtual = novoEstado;
    frameAtual = 0;
    tempoUltimoFrame = millis();
    
    if (estadoAtual == IDLE) {
        intervaloPiscar = random(2000, 6000);
    } else if (estadoAtual == THINKING) {
        frameAtual = random(0, 4); // Já inicia o THINKING com uma cara aleatória
    }
    
    desenharRostoState(); // Força o desenho imediato
  }
}

void atualizarAnimacao() {
  unsigned long tempoAtual = millis();

  if (estadoAtual == IDLE) {
    if (frameAtual == 0) {
      if (tempoAtual - tempoUltimoFrame > intervaloPiscar) {
        frameAtual = 1; // Pisca
        tempoUltimoFrame = tempoAtual;
        desenharRostoState();
      }
    } else {
      if (tempoAtual - tempoUltimoFrame > 150) { // Tempo do piscar rápido
        frameAtual = 0; // Olho aberto
        tempoUltimoFrame = tempoAtual;
        intervaloPiscar = random(2000, 5000); // Sorteia o próximo piscar
        desenharRostoState();
      }
    }
  } 
  else {
    // Estados animados contínuos: LISTENING, THINKING, SPEAKING
    int intervalo = 200; 
    int maxFrames = 1;

    if (estadoAtual == LISTENING) { intervalo = 500; maxFrames = 2; }
    else if (estadoAtual == THINKING) { intervalo = 3000; maxFrames = 4; } // Muda a cada 3 segundos
    else if (estadoAtual == SPEAKING) { intervalo = 150; maxFrames = 3; }

    if (tempoAtual - tempoUltimoFrame > intervalo) {
      if (estadoAtual == THINKING) {
        // Lógica randômica exclusiva para o modo THINKING
        int proximoFrame = random(0, 4);
        while (proximoFrame == frameAtual) { // Evita repetir a exata mesma expressão
          proximoFrame = random(0, 4);
        }
        frameAtual = proximoFrame;
      } else {
        frameAtual = (frameAtual + 1) % maxFrames;
      }
      
      tempoUltimoFrame = tempoAtual;
      desenharRostoState();
    }
  }
}

void desenharRostoState() {
  u8g2.clearBuffer(); 
  
  // Coordenadas centrais calibradas para 192x96
  int olhoEsqX = 66, olhoEsqY = 35;
  int olhoDirX = 126, olhoDirY = 35;
  int bocaX = 96, bocaY = 60;

  switch(estadoAtual) {
    case IDLE:
      if (frameAtual == 0) {
        // Olhos abertos padrao
        u8g2.drawDisc(olhoEsqX, olhoEsqY, 6);
        u8g2.drawDisc(olhoDirX, olhoDirY, 6);
      } else {
        // idle 01.png - Olhos fechados
        u8g2.drawLine(olhoEsqX - 8, olhoEsqY, olhoEsqX + 8, olhoEsqY);
        u8g2.drawLine(olhoDirX - 8, olhoDirY, olhoDirX + 8, olhoDirY);
      }
      // Boca sorriso leve
      u8g2.drawCircle(bocaX, bocaY - 5, 12, U8G2_DRAW_LOWER_RIGHT | U8G2_DRAW_LOWER_LEFT);
      break;

    case LISTENING:
      if (frameAtual == 0) {
        // listen 01.png
        u8g2.drawDisc(olhoEsqX, olhoEsqY, 6);
        u8g2.drawDisc(olhoDirX, olhoDirY, 6);
      } else {
        // listen 02.png (Olhos felizes em arco)
        u8g2.drawCircle(olhoEsqX, olhoEsqY+4, 8, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
        u8g2.drawCircle(olhoDirX, olhoDirY+4, 8, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
      }
      // Boca aberta feliz
      u8g2.drawCircle(bocaX, bocaY - 5, 12, U8G2_DRAW_LOWER_RIGHT | U8G2_DRAW_LOWER_LEFT);
      u8g2.drawLine(bocaX - 12, bocaY - 5, bocaX + 12, bocaY - 5);
      break;

    case THINKING:
      // Nova lógica do modo Thinking (Randomizada e a cada 3s)
      { 
        int offset = 0;
        bool isSmiling = false;

        // Configura as opções (Olhar Direita/Esquerda + Sorriso/Sério)
        if (frameAtual == 0) { offset = -8; isSmiling = true; }       // Esquerda + Sorriso (thinking 01_2)
        else if (frameAtual == 1) { offset = 8; isSmiling = true; }   // Direita + Sorriso (thinking 02_2)
        else if (frameAtual == 2) { offset = -8; isSmiling = false; } // Esquerda + Sério (thinking 03_2)
        else if (frameAtual == 3) { offset = 8; isSmiling = false; }  // Direita + Sério (thinking 04_2)

        // Desenha as pupilas deslocadas
        u8g2.drawDisc(olhoEsqX + offset, olhoEsqY, 6);
        u8g2.drawDisc(olhoDirX + offset, olhoDirY, 6);

        // Desenha a sobrancelha (risquinho) logo acima dos olhos
        u8g2.drawLine(olhoEsqX + offset - 10, olhoEsqY - 7, olhoEsqX + offset + 10, olhoEsqY - 7);
        u8g2.drawLine(olhoDirX + offset - 10, olhoDirY - 7, olhoDirX + offset + 10, olhoDirY - 7);

        // Desenha a boca de acordo com o frame
        if (isSmiling) {
          u8g2.drawCircle(bocaX, bocaY - 5, 12, U8G2_DRAW_LOWER_RIGHT | U8G2_DRAW_LOWER_LEFT);
        } else {
          u8g2.drawLine(bocaX - 10, bocaY, bocaX + 10, bocaY);
        }
      }
      break;

    case SPEAKING:
      // speaking 01 a 03
      u8g2.drawDisc(olhoEsqX, olhoEsqY, 6);
      u8g2.drawDisc(olhoDirX, olhoDirY, 6);

      if (frameAtual == 0) {
        // speaking 01.png - Boca fechada
        u8g2.drawLine(bocaX - 12, bocaY, bocaX + 12, bocaY);
      } else if (frameAtual == 1) {
        // speaking 02.png - Boca meio aberta
        u8g2.drawRBox(bocaX - 10, bocaY - 4, 20, 8, 3);
      } else if (frameAtual == 2) {
        // speaking 03.png - Boca bem aberta (Rounded Box)
        u8g2.drawRBox(bocaX - 14, bocaY - 8, 28, 16, 5);
      }
      break;
  }
  
  u8g2.sendBuffer(); 
}

void enviarMensagem(String mensagem) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  // Feedback visual inicial
  mudarEstado(LISTENING); 
  atualizarAnimacao();
  delay(1000); // Dá um tempinho para o rosto sorrir

  // Estado de processamento durante a rede
  mudarEstado(THINKING); 
  Serial.println("\nAbrindo socket TCP direto...");
  
  WiFiClient client;
  if (!client.connect("192.168.10.5", 8000)) {
    Serial.println("Falha na conexão TCP.");
    mudarEstado(IDLE); // Erro, volta ao normal
    return;
  }

  StaticJsonDocument<256> doc;
  doc["mensagem"] = mensagem;
  String requestBody;
  serializeJson(doc, requestBody);

  Serial.println("Enviando pacote HTTP manual e aguardando IA (Timeout de 60s)...");

  client.println("POST /api/bmo HTTP/1.1");
  client.println("Host: 192.168.10.5");
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(requestBody.length());
  client.println("Connection: close");
  client.println(); 
  client.print(requestBody);

  unsigned long timeout = millis();
  
  // Loop de espera não-bloqueante
  while (client.connected() && !client.available()) {
    if (millis() - timeout > 60000) { 
      Serial.println("Timeout aguardando a API!");
      client.stop();
      mudarEstado(IDLE);
      return;
    }
    
    // A animação de THINKING continua rodando a cada 3s aqui dentro!
    atualizarAnimacao(); 
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

    file = new AudioFileSourceHTTPStream(audioUrl);
    mp3->begin(file, out);
    
    // A API respondeu com sucesso, muda para falando
    mudarEstado(SPEAKING); 
    
  } else {
    Serial.println("Erro ao decodificar JSON.");
    mudarEstado(IDLE);
  }
  
  client.stop(); 
}
