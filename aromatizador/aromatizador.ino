// Aromatizador automatico v2 - ESP-01 + adaptador RoboCore + L298N
// IO0 -> IN1 | IO2 -> IN2 | RXH (GPIO3) -> botao Key (outro fio no GND)
// Placa na IDE: "Generic ESP8266 Module", Flash Size "1MB (FS:64KB OTA:~470KB)"
// Acesse: http://aromatizador.local  (ou o IP do ESP no roteador)
//
// Novidades:
// - Data e hora pela internet (NTP), fuso de Brasilia
// - Bloqueio por horario, dia da semana e feriados nacionais (calculados no proprio ESP)
// - Lista de datas extras bloqueadas (feriados municipais, emendas, ferias)
// - Contadores salvos na memoria flash: total, desde a troca do refil, refis usados e por dia
// - Historico diario para download em CSV

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <time.h>

// ===== Rede Wi-Fi =====
// Nome e senha ficam em segredos.h (fora do git). Para criar:
// copie segredos.h.exemplo para segredos.h e preencha.
#include "segredos.h"

// Fuso de Brasilia (UTC-3, sem horario de verao)
const char* FUSO = "<-03>3";

// ===== Pinos =====
const int IN1   = 0;  // IO0
const int IN2   = 2;  // IO2
const int BOTAO = 3;  // RX (RXH no adaptador)

// ===== Tempos do motor (fixos) =====
const uint16_t T_DESCE = 500;  // ms motor descendo
const uint16_t T_PAUSA = 200;  // ms parado com a valvula apertada
const uint16_t T_VOLTA = 500;  // ms motor voltando

// ===== Configuracao (EEPROM) =====
struct Cfg {
  uint16_t intervaloMin;      // minutos entre disparos
  uint16_t inicioMin;         // inicio do horario permitido (minutos desde 00:00)
  uint16_t fimMin;            // fim do horario permitido
  uint8_t  dias;              // dias permitidos: bit0 = domingo ... bit6 = sabado
  uint8_t  ativo;             // disparos automaticos ligados
  uint8_t  bloqFeriados;      // bloquear feriados nacionais
  uint8_t  bloqFacultativos;  // bloquear tambem Carnaval e Corpus Christi
  char     extras[160];       // datas extras: "24/06, 08/12, 26/12/2026"
  uint8_t  marca;             // valida a EEPROM
} cfg;

const uint8_t MARCA = 0xB3;  // mude ao alterar a struct Cfg

// ===== Contadores (arquivo na flash, sobrevive a quedas de energia) =====
struct Cont {
  uint32_t total;  // todos os disparos desde sempre
  uint32_t refil;  // desde a ultima troca de refil
  uint32_t hoje;   // disparos do dia atual
  uint32_t dia;    // dia atual no formato AAAAMMDD
  uint32_t refis;       // quantos refis ja foram trocados (soma a cada "Troquei o refil")
  uint32_t trocasHoje;  // trocas de refil no dia atual (vai para o historico)
} cont;

// Tamanho do arquivo mais antigo (so os 4 primeiros campos). Arquivos menores que a
// struct atual sao lidos assim mesmo: os campos novos, no fim, ficam em 0.
const size_t TAM_CONT_V1 = 4 * sizeof(uint32_t);

const char* CAB_CSV = "data;disparos;trocas_refil\n";

ESP8266WebServer server(80);
unsigned long ultimo = 0;
const char* NOMES_DIAS[] = {"Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sab"};

// ===================================================================
// Armazenamento
// ===================================================================
void salvarCfg() { EEPROM.put(0, cfg); EEPROM.commit(); }

void carregarCfg() {
  EEPROM.begin(sizeof(Cfg));
  EEPROM.get(0, cfg);
  if (cfg.marca != MARCA) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.intervaloMin = 15;
    cfg.inicioMin = 8 * 60;  // 08:00
    cfg.fimMin = 18 * 60;    // 18:00
    cfg.dias = 0b0111110;    // segunda a sexta
    cfg.ativo = 1;
    cfg.bloqFeriados = 1;
    cfg.bloqFacultativos = 1;
    cfg.marca = MARCA;
    salvarCfg();
  }
  cfg.extras[sizeof(cfg.extras) - 1] = 0;
}

void salvarCont() {
  File f = LittleFS.open("/cont.bin", "w");
  if (!f) return;
  f.write((const uint8_t*)&cont, sizeof(cont));
  f.close();
}

void carregarCont() {
  memset(&cont, 0, sizeof(cont));
  File f = LittleFS.open("/cont.bin", "r");
  if (f) {
    size_t n = f.size();
    if (n >= TAM_CONT_V1 && n <= sizeof(cont) && n % sizeof(uint32_t) == 0)
      f.read((uint8_t*)&cont, n);
    f.close();
  }
}

// Converte o historico antigo ("data;disparos") para o formato com a coluna
// trocas_refil, preenchendo 0 nas linhas antigas. Roda uma vez, no boot.
void migrarHistorico() {
  File f = LittleFS.open("/historico.csv", "r");
  if (!f) return;
  String cab = f.readStringUntil('\n');
  cab.trim();
  if (cab != "data;disparos") { f.close(); return; }

  File g = LittleFS.open("/historico.tmp", "w");
  if (!g) { f.close(); return; }
  g.print(CAB_CSV);
  while (f.available()) {
    String linha = f.readStringUntil('\n');
    linha.trim();
    if (linha.length()) { g.print(linha); g.print(";0\n"); }
  }
  f.close();
  g.close();
  LittleFS.remove("/historico.csv");
  LittleFS.rename("/historico.tmp", "/historico.csv");
}

// Linha do CSV para o dia guardado em cont.dia
void linhaDia(char* b, size_t tam) {
  snprintf(b, tam, "%02lu/%02lu/%04lu;%lu;%lu\n",
           (unsigned long)(cont.dia % 100),
           (unsigned long)((cont.dia / 100) % 100),
           (unsigned long)(cont.dia / 10000),
           (unsigned long)cont.hoje,
           (unsigned long)cont.trocasHoje);
}

// ===================================================================
// Data e hora
// ===================================================================
bool agoraTm(struct tm& t) {
  time_t n = time(nullptr);
  if (n < 1700000000) return false;  // ainda nao sincronizou com a internet
  localtime_r(&n, &t);
  return true;
}

// Fecha o dia anterior no historico quando a data muda
void verificaDia() {
  struct tm t;
  if (!agoraTm(t)) return;
  uint32_t hoje = (t.tm_year + 1900) * 10000UL + (t.tm_mon + 1) * 100UL + t.tm_mday;
  if (cont.dia == hoje) return;

  if (cont.dia != 0) {
    File f = LittleFS.open("/historico.csv", "a");
    if (f) {
      if (f.size() == 0) f.print(CAB_CSV);
      char b[48];
      linhaDia(b, sizeof(b));
      f.print(b);
      f.close();
    }
    cont.hoje = 0;
    cont.trocasHoje = 0;
  }
  cont.dia = hoje;
  salvarCont();
}

// ===================================================================
// Feriados
// ===================================================================
// Domingo de Pascoa (algoritmo de Meeus/Butcher)
void pascoa(int ano, int& mes, int& dia) {
  int a = ano % 19, b = ano / 100, c = ano % 100, d = b / 4, e = b % 4;
  int f = (b + 8) / 25, g = (b - f + 1) / 3;
  int h = (19 * a + b - d - g + 15) % 30;
  int i = c / 4, k = c % 4;
  int l = (32 + 2 * e + 2 * i - h - k) % 7;
  int m = (a + 11 * h + 22 * l) / 451;
  mes = (h + l - 7 * m + 114) / 31;
  dia = ((h + l - 7 * m + 114) % 31) + 1;
}

// Hoje e o dia "Pascoa + deslocamento"?
bool diaPascoaMais(const struct tm& t, int deslocamento) {
  int pm, pd;
  pascoa(t.tm_year + 1900, pm, pd);
  struct tm x = {};
  x.tm_year = t.tm_year;
  x.tm_mon = pm - 1;
  x.tm_mday = pd + deslocamento;
  x.tm_hour = 12;
  mktime(&x);  // normaliza a data
  return x.tm_mon == t.tm_mon && x.tm_mday == t.tm_mday;
}

// Datas extras digitadas na pagina: DD/MM (todo ano) ou DD/MM/AAAA (so naquele ano)
bool extraHoje(const struct tm& t) {
  const char* p = cfg.extras;
  while (*p) {
    int d, m, a = 0, n = 0;
    if (sscanf(p, "%d/%d%n", &d, &m, &n) == 2 && n > 0) {
      const char* q = p + n;
      if (*q == '/') {
        int n2 = 0;
        if (sscanf(q + 1, "%d%n", &a, &n2) == 1) q += 1 + n2;
        else q++;
      }
      if (d == t.tm_mday && m == t.tm_mon + 1 && (a == 0 || a == t.tm_year + 1900)) return true;
      p = q;
    } else {
      p++;
    }
  }
  return false;
}

// Retorna o nome do feriado de hoje, ou "" se nao for feriado
String feriadoHoje(const struct tm& t) {
  static const struct { uint8_t d, m; const char* nome; } fixos[] = {
    {1, 1, "Confraternizacao Universal"},
    {21, 4, "Tiradentes"},
    {1, 5, "Dia do Trabalho"},
    {7, 9, "Independencia"},
    {12, 10, "Nossa Senhora Aparecida"},
    {2, 11, "Finados"},
    {15, 11, "Proclamacao da Republica"},
    {20, 11, "Consciencia Negra"},
    {25, 12, "Natal"},
  };
  for (auto& f : fixos)
    if (f.d == t.tm_mday && f.m == t.tm_mon + 1) return f.nome;

  if (diaPascoaMais(t, -2)) return "Sexta-feira Santa";
  if (cfg.bloqFacultativos) {
    if (diaPascoaMais(t, -48) || diaPascoaMais(t, -47)) return "Carnaval";
    if (diaPascoaMais(t, 60)) return "Corpus Christi";
  }
  if (extraHoje(t)) return "Data extra bloqueada";
  return "";
}

// ===================================================================
// Regras de bloqueio: retorna o motivo, ou "" se estiver liberado
// ===================================================================
String motivoBloqueio() {
  struct tm t;
  if (!agoraTm(t)) return "aguardando data e hora da internet";
  if (!(cfg.dias & (1 << t.tm_wday))) return "dia da semana bloqueado";
  if (cfg.bloqFeriados) {
    String f = feriadoHoje(t);
    if (f.length()) return "feriado (" + f + ")";
  }
  int agora = t.tm_hour * 60 + t.tm_min;
  bool dentro = (cfg.inicioMin <= cfg.fimMin)
                  ? (agora >= cfg.inicioMin && agora < cfg.fimMin)
                  : (agora >= cfg.inicioMin || agora < cfg.fimMin);  // janela que passa da meia-noite
  if (!dentro) return "fora do horario permitido";
  return "";
}

// ===================================================================
// Motor
// ===================================================================
void motor(int a, int b) { digitalWrite(IN1, a); digitalWrite(IN2, b); }
void parar() { motor(HIGH, HIGH); }  // freio (mesmo estado do boot)

void disparar() {
  motor(HIGH, LOW);  delay(T_DESCE);  // desce e aperta a valvula
  parar();           delay(T_PAUSA);  // segura / freia antes de inverter
  motor(LOW, HIGH);  delay(T_VOLTA);  // volta ao repouso
  parar();
  ultimo = millis();

  verificaDia();
  cont.total++;
  cont.refil++;
  cont.hoje++;
  salvarCont();
}

// ===================================================================
// Pagina web
// ===================================================================
int argInt(const char* n, int minimo, int maximo, int atual) {
  if (!server.hasArg(n) || server.arg(n).length() == 0) return atual;
  return constrain(server.arg(n).toInt(), minimo, maximo);
}

String hm(uint16_t m) {
  char b[6];
  snprintf(b, sizeof(b), "%02u:%02u", m / 60, m % 60);
  return String(b);
}

uint16_t lerHM(const String& s, uint16_t atual) {
  int h, m;
  if (sscanf(s.c_str(), "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) return h * 60 + m;
  return atual;
}

void campoNum(String& h, const char* rotulo, const char* nome, int valor) {
  h += F("<label>"); h += rotulo;
  h += F("<input type=number name="); h += nome;
  h += F(" value="); h += valor; h += F("></label>");
}

void caixa(String& h, const char* nome, bool marcado, const char* texto) {
  h += F("<label><input type=checkbox name="); h += nome;
  if (marcado) h += F(" checked");
  h += F("> "); h += texto; h += F("</label>");
}

void pagina() {
  bool acao = false;

  if (server.hasArg("salvar")) {
    cfg.intervaloMin = argInt("i", 1, 1440, cfg.intervaloMin);
    cfg.inicioMin = lerHM(server.arg("hi"), cfg.inicioMin);
    cfg.fimMin = lerHM(server.arg("hf"), cfg.fimMin);
    cfg.dias = 0;
    for (int i = 0; i < 7; i++)
      if (server.hasArg("w" + String(i))) cfg.dias |= (1 << i);
    cfg.ativo = server.hasArg("a");
    cfg.bloqFeriados = server.hasArg("bf");
    cfg.bloqFacultativos = server.hasArg("bo");
    String x = server.arg("x");
    x.replace("'", "");
    x.replace("<", "");
    strlcpy(cfg.extras, x.c_str(), sizeof(cfg.extras));
    salvarCfg();
    acao = true;
  }
  if (server.hasArg("agora")) { disparar(); acao = true; }  // manual ignora bloqueios
  if (server.hasArg("trocaRefil")) {  // conta mais um refil usado e recomeca a contagem de disparos
    verificaDia();                    // fecha o dia anterior antes de anotar a troca em hoje
    cont.refis++;
    cont.trocasHoje++;
    cont.refil = 0;
    salvarCont();
    acao = true;
  }

  // Depois de uma acao, recarrega a pagina limpa (evita repetir ao atualizar)
  if (acao) {
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }

  struct tm t;
  bool temHora = agoraTm(t);
  char dataHora[24] = "sem data e hora";
  if (temHora) strftime(dataHora, sizeof(dataHora), "%d/%m/%Y %H:%M", &t);
  String bloq = motivoBloqueio();
  long falta = (long)(cfg.intervaloMin * 60UL) - (long)((millis() - ultimo) / 1000UL);
  if (falta < 0) falta = 0;

  String h;
  h.reserve(5000);
  h += F("<!doctype html><html><head><meta charset=utf-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>Aromatizador</title><style>"
         "body{font-family:sans-serif;max-width:440px;margin:20px auto;padding:0 16px;color:#222}"
         "h3{margin:22px 0 6px}label{display:block;margin:8px 0}"
         "input[type=number],input[type=time],input[type=text]{width:100%;padding:8px;font-size:16px;box-sizing:border-box}"
         ".dias label{display:inline-block;margin-right:10px}"
         "button,a.b{display:block;width:100%;padding:12px;margin-top:10px;font-size:16px;text-align:center;"
         "border:0;border-radius:8px;background:#2d6cdf;color:#fff;text-decoration:none;box-sizing:border-box}"
         "a.v{background:#1d9e75}a.c{background:#777}"
         ".card{background:#f2f2f2;border-radius:8px;padding:10px 12px;font-size:14px;line-height:1.7}"
         ".ok{color:#1d7a4f;font-weight:bold}.no{color:#b33;font-weight:bold}"
         ".n{font-size:13px;color:#666}</style></head><body><h2>Aromatizador</h2>");

  // Status
  h += F("<div class=card>");
  h += dataHora;
  if (temHora) { h += F(" ("); h += NOMES_DIAS[t.tm_wday]; h += F(")"); }
  h += F("<br>");
  if (!cfg.ativo) {
    h += F("<span class=no>Automatico desligado</span>");
  } else if (bloq.length()) {
    h += F("<span class=no>Bloqueado:</span> "); h += bloq;
  } else {
    h += F("<span class=ok>Liberado</span>, proximo em ");
    h += falta / 60; h += F(" min "); h += falta % 60; h += F(" s");
  }
  h += F("<br>Hoje: "); h += cont.hoje;
  h += F(" | Neste refil: "); h += cont.refil;
  h += F(" | Total: "); h += cont.total;
  h += F("<br>Refis usados: "); h += cont.refis;
  h += F("</div>");

  // Diagnostico
  unsigned long seg = millis() / 1000UL;
  h += F("<p class=n>Sinal: "); h += WiFi.RSSI(); h += F(" dBm | IP: ");
  h += WiFi.localIP().toString();
  h += F("<br>Ligado ha "); h += seg / 86400UL; h += F("d ");
  h += (seg / 3600UL) % 24; h += F("h "); h += (seg / 60UL) % 60; h += F("min");
  h += F("<br>Ultimo reinicio: "); h += ESP.getResetReason(); h += F("</p>");

  // Acoes
  h += F("<a class='b v' href='/?agora=1'>Disparar agora</a>"
         "<a class='b c' href='/?trocaRefil=1' onclick=\"return confirm('Registrar a troca do refil?')\">Troquei o refil</a>"
         "<a class='b c' href='/historico.csv'>Baixar historico (CSV)</a>");

  // Configuracao
  h += F("<form><input type=hidden name=salvar value=1><h3>Disparo</h3>");
  campoNum(h, "Intervalo (min)", "i", cfg.intervaloMin);
  caixa(h, "a", cfg.ativo, "Disparos automaticos");

  h += F("<h3>Horario permitido</h3><label>Inicio<input type=time name=hi value=");
  h += hm(cfg.inicioMin);
  h += F("></label><label>Fim<input type=time name=hf value=");
  h += hm(cfg.fimMin);
  h += F("></label><div class=dias>");
  for (int i = 0; i < 7; i++) {
    String nome = "w" + String(i);
    caixa(h, nome.c_str(), cfg.dias & (1 << i), NOMES_DIAS[i]);
  }
  h += F("</div>");

  h += F("<h3>Feriados</h3>");
  caixa(h, "bf", cfg.bloqFeriados, "Bloquear feriados nacionais");
  caixa(h, "bo", cfg.bloqFacultativos, "Bloquear tambem Carnaval e Corpus Christi");
  h += F("<label>Datas extras bloqueadas<input type=text name=x maxlength=159 "
         "placeholder='24/06, 08/12, 26/12/2026' value='");
  h += cfg.extras;
  h += F("'></label><p class=n>DD/MM bloqueia todo ano. DD/MM/AAAA bloqueia so naquele ano. "
         "Separe por virgula. O botao fisico e o Disparar agora funcionam mesmo bloqueados.</p>"
         "<button>Salvar</button></form></body></html>");

  server.send(200, "text/html", h);
}

// Envia o historico salvo e, no fim, a linha parcial do dia atual
void historico() {
  verificaDia();
  server.sendHeader("Content-Disposition", "attachment; filename=historico.csv");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");

  File f = LittleFS.open("/historico.csv", "r");
  if (f && f.size() > 0) {
    char buf[256];
    size_t n;
    while ((n = f.read((uint8_t*)buf, sizeof(buf))) > 0) server.sendContent(buf, n);
  } else {
    server.sendContent(CAB_CSV);
  }
  if (f) f.close();

  if (cont.dia != 0) {
    char b[48];
    linhaDia(b, sizeof(b));
    server.sendContent(b);
  }
  server.sendContent("");  // encerra a resposta
}

// ===================================================================
// Setup / loop
// ===================================================================
void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  parar();
  pinMode(BOTAO, INPUT_PULLUP);  // nao usar Serial: o RX virou entrada do botao

  carregarCfg();
  LittleFS.begin();
  carregarCont();
  migrarHistorico();

  WiFi.persistent(false);              // nao regrava credenciais na flash a cada boot
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);  // radio sempre acordado: pagina responde sempre
  WiFi.setAutoReconnect(true);
  WiFi.hostname("aromatizador");
  WiFi.begin(WIFI_NOME, WIFI_SENHA);

  configTime(FUSO, "a.st1.ntp.br", "pool.ntp.org");

  MDNS.begin("aromatizador");
  server.on("/", pagina);
  server.on("/historico.csv", historico);
  server.begin();

  ultimo = millis();  // primeiro disparo automatico so depois de um intervalo
}

void loop() {
  server.handleClient();
  MDNS.update();

  // Vigia do Wi-Fi: tenta reconectar e, se ficar 10 min sem rede, reinicia o ESP
  static unsigned long semRedeDesde = 0;
  static unsigned long ultimaTentativa = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (semRedeDesde == 0) semRedeDesde = millis();
    if (millis() - ultimaTentativa >= 30000) {
      ultimaTentativa = millis();
      WiFi.reconnect();
    }
    if (millis() - semRedeDesde >= 600000UL) ESP.restart();
  } else {
    semRedeDesde = 0;
  }

  // Verifica regras a cada 5 segundos
  static unsigned long ultimaChecagem = 0;
  if (millis() - ultimaChecagem >= 5000) {
    ultimaChecagem = millis();
    verificaDia();
    if (cfg.ativo && millis() - ultimo >= cfg.intervaloMin * 60000UL && motivoBloqueio().length() == 0)
      disparar();
  }

  // Botao fisico (ignora bloqueios)
  if (digitalRead(BOTAO) == LOW) {
    delay(30);  // anti-repique
    if (digitalRead(BOTAO) == LOW) {
      disparar();
      while (digitalRead(BOTAO) == LOW) delay(10);  // espera soltar
    }
  }
}
