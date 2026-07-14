# 🤖 Projeto BMO - Assistente IA Local com ESP32

Um assistente virtual interativo inspirado no BMO (Adventure Time), construído sobre um **NodeMCU-32S (ESP32)**. O projeto captura áudio do usuário (Push-to-Talk), comunica-se com um servidor de IA local e responde com voz (TTS) enquanto exibe animações faciais dinâmicas perfeitamente sincronizadas com seu estado operacional.

---

## 📌 Visão Geral

Este projeto integra hardware de áudio I2S (Microfone e Amplificador) e um display gráfico SPI para criar um robô de mesa independente. Todo o processamento pesado de IA (LLM, Speech-to-Text e Text-to-Speech) é delegado a um servidor local na rede Wi-Fi, permitindo que o ESP32 atue como um cliente *thin* focado puramente em interatividade de hardware, animação de interface em tempo real e streaming de dados.

## ⚙️ Funcionalidades

* **🎤 Push-to-Talk (PTT):** Captura de voz em tempo real através do microfone I2S.
* **🔊 Áudio de Alta Qualidade:** Reprodução de respostas da IA via amplificador I2S.
* **😊 Animações Dinâmicas (FSM):** Máquina de estados finita fluida com expressões (Ocioso, Ouvindo, Pensando, Falando).
* **🔄 Streaming Não-Bloqueante:** O display continua sendo animado independentemente do loop de rede e requisições HTTP estarem bloqueadas aguardando a resposta da IA.
* **📡 Cliente HTTP Raw PCM:** Envio direto de buffers de áudio em formato PCM bruto (16kHz, 16-bit Mono) para o servidor, poupando processamento no microcontrolador.

---

## 🛠️ Hardware Necessário

* **Microcontrolador:** NodeMCU-32S (ESP32)
* **Display:** JLX19296G-770 (LCD/OLED 192x96 via SPI)
* **Microfone:** Módulo INMP441 (Microfone Omnidirecional I2S)
* **Amplificador de Áudio:** Módulo MAX98357A (DAC I2S com amplificador Classe D) + Alto-falante de 3W/4Ω
* **Interação:** 3x Push Buttons (Chaves tácteis)

---

## 🔌 Mapeamento de Pinos (Wiring)

### Display (JLX19296G-770 - SPI)
| Pino Display | Pino ESP32 (NodeMCU-32S) | Função |
| :--- | :--- | :--- |
| `D0` (CLK) | `GPIO 18` | Clock SPI |
| `D1/D2/D3` (MOSI) | `GPIO 23` | Data In (MOSI) |
| `CS` | `GPIO 5` | Chip Select |
| `RS / DC` | `GPIO 17` | Data/Command |
| `RST` | `GPIO 16` | Reset |
| `LEDA` | `GPIO 4` | Backlight (PWM) |

### Amplificador de Áudio (MAX98357A - I2S_NUM_0)
| Pino MAX98357A | Pino ESP32 | Função |
| :--- | :--- | :--- |
| `BCLK` | `GPIO 26` | Bit Clock |
| `LRC / WSEL` | `GPIO 27` | Word Select (Left/Right Clock) |
| `DIN` | `GPIO 22` | Data In |
| `VCC` | `3.3V` ou `5V` | Alimentação |
| `GND` | `GND` | Terra |

### Microfone (INMP441 - I2S_NUM_1)
| Pino INMP441 | Pino ESP32 | Função |
| :--- | :--- | :--- |
| `SCK` | `GPIO 14` | Serial Clock |
| `WS` | `GPIO 15` | Word Select |
| `SD` | `GPIO 13` | Serial Data Out |
| `L/R` | `GND` | Define o canal esquerdo |
| `VDD` | `3.3V` | Alimentação |

### Botões (Pull-Up Interno)
| Botão | Pino ESP32 | Ação |
| :--- | :--- | :--- |
| `BTN_1` | `GPIO 32` | Pressione e segure para falar (Push-to-Talk) |
| `BTN_2` | `GPIO 33` | Atalho: Imitação de robô com defeito |
| `BTN_3` | `GPIO 25` | Atalho: Pergunta existencial |

---

## 💻 Software e Dependências

Para compilar este projeto, você precisará instalar as seguintes bibliotecas na Arduino IDE:

1. **`U8g2`** (por oliver): Para o controle gráfico e renderização dos frames do BMO no display.
2. **`ArduinoJson`** (por Benoit Blanchon): Para a montagem e parsing dos pacotes de comunicação com a IA local.
3. **`ESP8266Audio`** (por Earle F. Philhower, III): Fundamental para streaming HTTP de MP3 e comunicação I2S (`AudioGeneratorMP3`, `AudioFileSourceHTTPStream`, `AudioOutputI2S`).

*Nota:* A biblioteca `<driver/i2s.h>` já é nativa do pacote de placas ESP32 da Espressif.

---

## 🚀 Instalação e Uso

### 1. Preparando o Ambiente
Utilizando comandos Git, clone seu repositório local (caso o código esteja versionado) ou simplesmente crie um novo diretório de projeto na sua máquina:
```bash
git clone https://seu-repositorio/bmo-esp32.git
cd bmo-esp32
```

### 2. Configurações Iniciais
No arquivo `.ino` principal, edite as seguintes diretrizes para corresponder à sua rede local e ao IP do seu servidor de IA:

```cpp
// Substitua pelas credenciais da sua rede Wi-Fi
const char* ssid = "NOME_DA_SUA_REDE";
const char* password = "SENHA_DA_REDE";

// Nas funções enviarMensagem() e enviarAudioParaIA()
// Modifique o IP xxx.xxx.xx.x para o IP da máquina onde o modelo local está rodando
client.connect("xxx.xxx.xx.x", 8000);
```

### 3. Compilação
* Selecione a placa **NodeMCU-32S** na Arduino IDE.
* Verifique se o particionamento (Partition Scheme) tem espaço suficiente para o rádio Wi-Fi e bibliotecas pesadas (Recomenda-se *Huge APP* se necessário).
* Compile e faça o upload.

---

## 📡 Integração com a API Backend (Servidor Local)

O BMO espera se comunicar com um servidor rodando na porta `8000`. O servidor deve possuir dois endpoints principais:

### 1. Endpoint de Texto (`POST /api/bmo`)
Recebe um comando via botões e devolve uma URL de áudio MP3 para o BMO baixar e reproduzir.
* **Requisição (JSON):** `{"mensagem": "BMO, me conte uma piada curta sobre eletrônica."}`
* **Resposta Esperada (JSON):** `{"audio_url": "http://xxx.xxx.xx.x:8000/media/resposta_1.mp3"}`

### 2. Endpoint de Voz (`POST /api/bmo/voice`)
Recebe os dados binários puros gravados pelo INMP441. O backend deve pegar esses bytes, processar (ex: Whisper), mandar para o LLM, gerar o MP3 (TTS) e devolver a URL.
* **Cabeçalhos:** `Content-Type: application/octet-stream`
* **Corpo (Payload):** Bytes contínuos (Raw PCM, 16kHz, 16-bits, Mono)
* **Resposta Esperada (JSON):** `{"audio_url": "http://xxx.xxx.xx.x:8000/media/resposta_2.mp3"}`

---

## 🎨 Arquitetura de Estados (FSM)

A interface visual do BMO é construída com uma máquina de estados finita que não bloqueia o fluxo principal da CPU. 

* **IDLE (Ocioso):** O BMO pisca aleatoriamente em intervalos de 2 a 6 segundos. Olhos abertos com um sorriso ameno.
* **LISTENING (Ouvindo):** Acionado ao segurar o botão de PTT. Olhos felizes em formato de arco (^^) e um sorriso grande enquanto a gravação acontece.
* **THINKING (Pensando):** O estado mais dinâmico. O BMO intercala entre 4 expressões diferentes a cada 3 segundos (olhando para a direita/esquerda, oscilando entre sorrisos confiantes e expressões de concentração extrema com as sobrancelhas franzidas) enquanto o socket HTTP aguarda o processamento do LLM local.
* **SPEAKING (Falando):** A sincronização visual ocorre no loop, movendo a boca do BMO entre três estados de abertura (fechada, média, totalmente aberta) enquanto o áudio MP3 via I2S é transmitido para os alto-falantes.

---
*Projeto idealizado para experimentação em engenharia embarcada e desenvolvimento de hardware de consumo integrado com inteligência artificial local.*
