# automatiza_aromatizador

Aromatizador automático baseado em **ESP8266 (ESP-01)** com ponte H **L298N**,
controlado por **interface web**. Projeto aberto — contribuições são bem-vindas.

## Funcionalidades

- Acionamento **programado por horário** (intervalo configurável)
- **Bloqueios inteligentes**: horário, dia da semana, **feriados nacionais**
  (calculados no próprio ESP) e **lista de datas extras** (feriados municipais,
  emendas, férias)
- **Data e hora pela internet** (NTP, fuso de Brasília)
- **Interface web** para configuração e acompanhamento
- **Contadores** persistidos na flash (total, desde a troca do refil e por dia)
- **Histórico diário** com download em CSV
- Acesso via `http://aromatizador.local` (ou pelo IP no roteador)

## Hardware

- ESP-01 + adaptador RoboCore
- Ponte H L298N
- Ligações: `IO0 -> IN1` | `IO2 -> IN2` | `RXH (GPIO3) -> botão Key` (outro fio no GND)

## Compilação (IDE Arduino)

- Placa: **Generic ESP8266 Module**
- Flash Size: **1MB (FS:64KB OTA:~470KB)**
- Sketch: [`aromatizador/aromatizador.ino`](aromatizador/aromatizador.ino)

## Configuração

Antes de compilar, ajuste as credenciais de Wi-Fi (`WIFI_NOME` / `WIFI_SENHA`)
no início do sketch. Os valores versionados são apenas placeholders
(`SUA_REDE_WIFI` / `SUA_SENHA_WIFI`) — **não** faça commit da sua senha real.

## Imagens

Fotos e demonstrações ficam na pasta [`imagens/`](imagens/).

## Contribuindo

Sugestões e melhorias (especialmente na interface web) são bem-vindas via
_issues_ e _pull requests_. Para discutir mudanças maiores, abra uma _issue_
primeiro.

## Licença

Distribuído sob a licença **MIT** — veja [`LICENSE`](LICENSE).
© 2026 Marcos Morais.
