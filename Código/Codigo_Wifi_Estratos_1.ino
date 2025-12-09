#include <WiFi.h>
#include <HTTPClient.h>
#include <PZEM004Tv30.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ----------------- CONFIGURACIÓN WIFI -------------------
const char* ssid = "";
const char* password = "";
const char* serverUrl = "https://smart-energy-unal.onrender.com/api/measurements"; 

const unsigned long intervalMs = 10000; // Intervalo de envío (10 segundos)
unsigned long previousMillis = 0;

bool wifiHabilitado = true;
bool wifiConectado = false;
unsigned long ultimoIntentoWifi = 0;

// ---------- CONFIGURACIÓN PANTALLA OLED 7 PINES ----------
#define ANCHO 128
#define ALTO 64

#define PIN_MOSI 23
#define PIN_CLK  18
#define PIN_DC   16
#define PIN_RESET 17
#define PIN_CS   5

Adafruit_SSD1306 display(ANCHO, ALTO, &SPI, PIN_DC, PIN_RESET, PIN_CS);

// ---------- CONFIGURACIÓN PZEM004T-V3 ----------
PZEM004Tv30 pzem(Serial2, 21, 22); // RX=21, TX=22 ESP32

bool pzemOK = false;
unsigned long ultimoIntentoPZEM = 0;

// ---------- BOTONES ----------
#define BOTON_SUMAR 14
#define BOTON_RESTAR 27
#define BOTON_ESTRATO 26  // NUEVO: Botón exclusivo para cambiar estrato

// ---------- VARIABLES ----------
int estrato = 3;
float tarifas[7] = {0, 387, 484, 680, 800, 960, 960}; // Estratos 1-6 (Costos reporte enel Noviembre 2025 Bogotá)

float energia_kWh_real = 0;       // energía real acumulada (cada 1s)
float energia_kWh_acumulada = 0;  // energía proyectada (tiempo simulado)

int modo = 0;
unsigned long presionadoDesde = 0;

// Tiempo simulado (minutos)
unsigned long tiempoSimuladoMin = 0;
unsigned long ultimoTiempoSimulado = 0;

// Acumulación real
unsigned long ultimoAcumulo = 0;

// Variables para debounce mejorado
unsigned long ultimaPulsacionSumar = 0;
unsigned long ultimaPulsacionRestar = 0;
unsigned long ultimaPulsacionEstrato = 0; 
const unsigned long debounceDelay = 250;

// Indicador visual de cambio de estrato
bool estratoCambiado = false;
unsigned long tiempoMostrarEstrato = 0;

// ---------- FUNCIONES ----------
bool botonPresionadoLargo(int pin) {
  if (digitalRead(pin) == LOW) {
    if (presionadoDesde == 0) presionadoDesde = millis();
    if (millis() - presionadoDesde >= 3000) {
      presionadoDesde = 0;
      return true;
    }
  } else presionadoDesde = 0;
  return false;
}

bool botonPresionado(int pin, unsigned long &ultimaPulsacion) {
  if (digitalRead(pin) == LOW) {
    if (millis() - ultimaPulsacion > debounceDelay) {
      ultimaPulsacion = millis();
      return true;
    }
  }
  return false;
}

void clean() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
}

//Mostrar indicador de estrato en todos los menús
void mostrarIndicadorEstrato() {
  // Mostrar indicador por 2 segundos después del cambio
  if (estratoCambiado && millis() - tiempoMostrarEstrato < 2000) {
    // Cuadro en la esquina inferior izquierda
    display.fillRect(0, 52, 35, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(3, 54);
    display.setTextSize(1);
    display.print("E:");
    display.print(estrato);
    display.setTextColor(SSD1306_WHITE);
  } else if (millis() - tiempoMostrarEstrato >= 2000) {
    estratoCambiado = false;
  }
}

void conectarWifi() {
  if (!wifiHabilitado) return;
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("Conectando WiFi...");
  display.display();
  
  Serial.println("\n=== Conectando WiFi ===");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConectado = true;
    Serial.println("\n✅ WiFi conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("WiFi Conectado!");
    display.print("IP: ");
    display.println(WiFi.localIP());
    display.display();
    delay(1500);
  } else {
    wifiConectado = false;
    Serial.println("\n❌ WiFi NO conectado");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi: Sin conexion");
    display.display();
    delay(1500);
  }
}

void verificarWifi() {
  if (!wifiHabilitado) {
    if (wifiConectado) {
      WiFi.disconnect();
      wifiConectado = false;
      Serial.println("WiFi deshabilitado por usuario");
    }
    return;
  }
  
  // Reintentar cada 30 segundos si no está conectado
  if (WiFi.status() != WL_CONNECTED && millis() - ultimoIntentoWifi > 30000) {
    ultimoIntentoWifi = millis();
    Serial.println("WiFi desconectado. Reconectando...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    
    int intentos = 0;
    while (WiFi.status() != WL_CONNECTED && intentos < 20) {
      delay(500);
      Serial.print(".");
      intentos++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      wifiConectado = true;
      Serial.println("\n✅ WiFi reconectado!");
    } else {
      wifiConectado = false;
    }
  }
}

void enviarDatos(float v, float c, float p, float pf) {
  if (!wifiHabilitado || !wifiConectado) return;
  
  unsigned long now = millis();
  if (now - previousMillis < intervalMs) return;
  previousMillis = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi NO conectado, no se envía.");
    wifiConectado = false;
    return;
  }

  float energia_total_pzem = pzem.energy();
  if (isnan(energia_total_pzem)) energia_total_pzem = 0;
  
  String payload = "{";
  payload += "\"device_id\": \"esp32_pzem\",";
  payload += "\"voltage\": " + String(v, 2) + ",";
  payload += "\"current\": " + String(c, 2) + ",";
  payload += "\"power\": " + String(p, 2) + ",";
  payload += "\"energy\": " + String(energia_total_pzem, 3);
  payload += "}";

  HTTPClient http;
  http.begin(serverUrl);
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");

  Serial.print("📤 Enviando datos: ");
  Serial.println(payload);

  int httpResponseCode = http.POST(payload);
  Serial.print("HTTP code: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode > 0) {
    Serial.println("Respuesta servidor:");
    Serial.println(http.getString());
  } else {
    Serial.print("❌ Error POST: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

// ---------- SETUP ----------
void setup() {
  pinMode(BOTON_SUMAR, INPUT_PULLUP);
  pinMode(BOTON_RESTAR, INPUT_PULLUP);
  pinMode(BOTON_ESTRATO, INPUT_PULLUP);

  Serial.begin(115200);
  
  // Inicializar pantalla
  display.begin(SSD1306_SWITCHCAPVCC);
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.println("Cargando...");
  display.display();
  delay(800);

  // Inicializar Serial2 para PZEM
  Serial2.begin(9600, SERIAL_8N1, 21, 22);
  delay(1000);

  // Probar comunicación con PZEM
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("Probando PZEM...");
  display.display();

  Serial.println("\n=== Probando PZEM ===");
  float testVolt = pzem.voltage();
  delay(500);

  if (!isnan(testVolt) && testVolt > 0) {
    pzemOK = true;
    Serial.println("✅ PZEM OK!");
    display.println("PZEM: OK");
  } else {
    pzemOK = false;
    Serial.println("❌ PZEM NO responde");
    Serial.println("Revisa conexiones RX/TX");
    display.println("PZEM: ERROR");
    display.println("Revisar cables!");
  }
  display.display();
  delay(2000);

  // Conectar WiFi
  conectarWifi();

  ultimoAcumulo = millis();
  ultimoTiempoSimulado = millis();
}

// ---------- LOOP ----------
void loop() {

  // Verificar y mantener WiFi
  verificarWifi();

  // Reintentar PZEM cada 10 segundos si no funciona
  if (!pzemOK && millis() - ultimoIntentoPZEM > 10000) {
    ultimoIntentoPZEM = millis();
    float testVolt = pzem.voltage();
    if (!isnan(testVolt) && testVolt > 0) {
      pzemOK = true;
      Serial.println("✅ PZEM reconectado!");
    } else {
      Serial.println("⚠️ PZEM aún sin responder...");
    }
  }

  // ---- Detectar cambio de estrato desde cualquier menú ----
  if (botonPresionado(BOTON_ESTRATO, ultimaPulsacionEstrato)) {
    estrato++;
    if (estrato > 6) estrato = 1;
    
    // Activar indicador visual
    estratoCambiado = true;
    tiempoMostrarEstrato = millis();
    
    Serial.print("📊 Estrato cambiado a: ");
    Serial.println(estrato);
  }

  // ---- Lectura PZEM con validación ----
  float volt = 0, amp = 0, watt = 0, pf = 0;

  if (pzemOK) {
    volt = pzem.voltage();
    delay(50);
    amp = pzem.current();
    delay(50);
    watt = pzem.power();
    delay(50);
    pf = pzem.pf();
    
    if (isnan(volt) || isnan(amp)) {
      pzemOK = false;
      Serial.println("⚠️ Lectura PZEM falló");
    }
  }

  if (isnan(volt)) volt = 0;
  if (isnan(amp)) amp = 0;
  if (isnan(watt)) watt = 0;
  if (isnan(pf)) pf = 0;

  // ---- Acumulación real (cada 1s) ----
  if (millis() - ultimoAcumulo >= 1000) {
    ultimoAcumulo = millis();
    energia_kWh_real += (watt / 1000.0) * (1.0 / 3600.0);
  }

  // ---- Tiempo simulado + acumulación proyectada ----
  if (millis() - ultimoTiempoSimulado >= 3000) {
    ultimoTiempoSimulado = millis();
    tiempoSimuladoMin += 30;

    float energiaInstantanea = watt * (30.0 / 60.0) / 1000.0;
    energia_kWh_acumulada += energiaInstantanea;
  }

  float consumo_kWh_proyectado = energia_kWh_acumulada;
  float costo_proyectado = consumo_kWh_proyectado * tarifas[estrato];

  // ---- ENVIAR DATOS POR WIFI ----
  enviarDatos(volt, amp, watt, pf);

  // ---- Pulsación larga → volver al menú 0 ----
  if (botonPresionadoLargo(BOTON_SUMAR) || botonPresionadoLargo(BOTON_RESTAR)) {
    modo = 0;
  }

  // ============================================================
  // ======================     MENÚS     ========================
  // ============================================================
  switch (modo) {

    // ------------------ MENÚ 0: COSTO ------------------
    case 0:
      if (botonPresionado(BOTON_SUMAR, ultimaPulsacionSumar)) {
        modo = 1;
      }

      clean();
      display.setTextSize(2);
      display.print("$");
      display.println(costo_proyectado, 2);

      display.setTextSize(1);
      display.setCursor(0, 28);
      display.print("kWh: ");
      display.println(consumo_kWh_proyectado, 4);

      display.setCursor(0, 42);
      display.print("Est: ");
      display.print(estrato);
      display.print("  T: ");
      display.print(tiempoSimuladoMin);
      display.println(" min");

      display.setCursor(0, 54);
      display.print("Tarifa: $");
      display.print(tarifas[estrato]);
      display.print("/kWh");
      
      // Indicador de WiFi
      if (wifiConectado && wifiHabilitado) {
        display.fillCircle(120, 4, 2, SSD1306_WHITE);
      }
      
      // Mostrar indicador de cambio de estrato
      mostrarIndicadorEstrato();
      
      // Alerta PZEM
      if (!pzemOK) {
        display.setCursor(0, 56);
        display.print("!PZEM ERROR!");
      }
      
      display.display();
      break;

    // ------------------ MENÚ 1: DATOS PZEM ------------------
    case 1:
      if (botonPresionado(BOTON_SUMAR, ultimaPulsacionSumar)) {
        modo = 2;
      }
      clean();
      display.setTextSize(1);
      display.print("V: ");
      display.println(volt, 2);
      display.print("A: ");
      display.println(amp, 3);
      display.print("W: ");
      display.println(watt, 2);
      display.print("PF: ");
      display.println(pf, 2);
      
      // Indicador de WiFi
      if (wifiConectado && wifiHabilitado) {
        display.fillCircle(120, 4, 2, SSD1306_WHITE);
      }
      
      // Mostrar indicador de cambio de estrato
      mostrarIndicadorEstrato();
      
      if (!pzemOK) {
        display.setCursor(0, 56);
        display.print("!PZEM ERROR!");
      }
      
      display.display();
      break;

    // ------------------ MENÚ 2: ESTIMACIÓN MENSUAL ------------------
    case 2: {
      if (botonPresionado(BOTON_SUMAR, ultimaPulsacionSumar)) {
        modo = 3;
      }

      float kWh_mes = (watt / 1000.0) * 24.0 * 30.0;
      float costo_mes = kWh_mes * tarifas[estrato];

      clean();
      display.setTextSize(1);
      display.println("Estimacion mensual");

      display.print("kWh mes: ");
      display.println(kWh_mes, 2);

      display.print("Costo mes: $");
      display.println(costo_mes, 0);

      display.print("Acum real: ");
      display.println(energia_kWh_real, 3);

      display.print("Acum proj: ");
      display.println(energia_kWh_acumulada, 3);
      
      if (wifiConectado && wifiHabilitado) {
        display.fillCircle(120, 4, 2, SSD1306_WHITE);
      }
      
      // Mostrar indicador de cambio de estrato
      mostrarIndicadorEstrato();
      
      if (!pzemOK) {
        display.setCursor(0, 56);
        display.print("!PZEM ERROR!");
      }

      display.display();
      break;
    }

    // ------------------ MENÚ 3: ESTADO WIFI ------------------
    case 3: {
      if (botonPresionado(BOTON_SUMAR, ultimaPulsacionSumar)) {
        modo = 0;
      }
      if (botonPresionado(BOTON_RESTAR, ultimaPulsacionRestar)) {
        wifiHabilitado = !wifiHabilitado;
        if (wifiHabilitado && !wifiConectado) {
          conectarWifi();
        }
      }

      clean();
      display.setTextSize(1);
      display.println("=== ESTADO WIFI ===");
      display.println();
      
      display.print("Estado: ");
      if (wifiHabilitado) {
        if (wifiConectado) {
          display.println("CONECTADO");
          display.print("IP: ");
          display.println(WiFi.localIP());
          display.print("RSSI: ");
          display.print(WiFi.RSSI());
          display.println(" dBm");
        } else {
          display.println("DESCONECTADO");
          display.println("Intentando...");
        }
      } else {
        display.println("DESHABILITADO");
      }
      
      display.println();
      display.print("Envio: ");
      if (wifiHabilitado && wifiConectado) {
        int segundos = (intervalMs - (millis() - previousMillis)) / 1000;
        if (segundos < 0) segundos = 0;
        display.print(segundos);
        display.println("s");
      } else {
        display.println("OFF");
      }
      
      // Mostrar indicador de cambio de estrato
      mostrarIndicadorEstrato();
      
      display.setCursor(0, 56);
      display.print("[RESTAR] ON/OFF");

      display.display();
      break;
    }
  }

  delay(200);
}
