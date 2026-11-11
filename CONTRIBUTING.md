# Contribuindo com o automatiza_aromatizador

Obrigado pelo interesse em contribuir! 🎉 Este é um projeto aberto (licença MIT)
e toda ajuda é bem-vinda — especialmente na **interface web**, na documentação e
em melhorias de firmware.

## Como contribuir

1. **Abra uma _issue_ primeiro** para bugs ou ideias maiores, descrevendo o que
   pretende. Para correções pequenas, pode ir direto ao _pull request_.
2. **Faça um _fork_** do repositório e crie um _branch_ a partir de `master`:
   ```
   git checkout -b minha-melhoria
   ```
3. Faça suas alterações e **teste no hardware** (veja abaixo).
4. **Commit** com mensagens claras (veja convenção abaixo).
5. Abra um **_pull request_** para `master`, preenchendo o template.

## Ambiente de desenvolvimento

- **Placa:** ESP8266 (ESP-01) — na IDE Arduino: *Generic ESP8266 Module*,
  Flash Size *1MB (FS:64KB OTA:~470KB)*.
- **Hardware:** ponte H L298N (`IO0 -> IN1`, `IO2 -> IN2`,
  `RXH (GPIO3) -> botão Key`).
- **Sketch:** [`aromatizador/aromatizador.ino`](aromatizador/aromatizador.ino).
- **Bibliotecas:** ESP8266WiFi, ESP8266WebServer, ESP8266mDNS, EEPROM, LittleFS
  (incluídas no core do ESP8266 para Arduino).

> ⚠️ **Nunca faça commit de credenciais reais.** Os campos `WIFI_NOME` /
> `WIFI_SENHA` devem permanecer como placeholders (`SUA_REDE_WIFI` /
> `SUA_SENHA_WIFI`). Ajuste localmente e não versione sua senha.

## Como testar

- Compile e grave na placa pela IDE Arduino.
- Acesse `http://aromatizador.local` (ou o IP do ESP no roteador) e valide:
  - acionamento manual e programado;
  - bloqueios por horário, dia da semana e feriados;
  - contadores e download do histórico em CSV.
- Descreva no PR **o que foi testado** e **como**.

## Convenção de commits

Mensagens curtas, no imperativo, em português. Exemplos:
- `Corrige cálculo de feriado móvel`
- `Adiciona botão de teste na interface web`
- `Melhora layout responsivo da página de configuração`

## Estilo de código

- Mantenha o padrão já existente no sketch (indentação, nomes em português).
- Comente trechos não óbvios.
- Evite introduzir dependências desnecessárias.

## Dúvidas

Abra uma _issue_ com a label `question`. Obrigado por contribuir! 🙌
