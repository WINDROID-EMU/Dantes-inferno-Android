# Guia de Compilação e Instalação no Android ARM64 (Dante's Inferno)

Este documento descreve como compilar, empacotar e executar o port de recompilação estática do **Dante's Inferno** (Xbox 360) em dispositivos **Android ARM64 (AArch64)**.

---

## 1. Requisitos

### Dispositivo Móvel (Android):
* **Processador:** ARM64 (aarch64 / ARMv8-A ou superior).
* **GPU:** Compatível com **Vulkan 1.1+** (GPUs Qualcomm Adreno 6xx/7xx/8xx oferecem melhor compatibilidade para os recursos de shader do Xbox 360 Xenos).
* **Sistema Operacional:** Android 8.0 (API 26) ou superior (Android 11+ recomendado).
* **Armazenamento:** ~8 GB livres para os dados do jogo extraídos da ISO.

### Ambiente de Desenvolvimento / Compilação:
* **Android NDK:** r25c ou superior (r26+ recomendado).
* **Android SDK:** API Level 34 com Build-Tools.
* **JDK:** Java Development Kit 17.
* **CMake:** 3.25+ e **Ninja**.
* **Clang:** Incluído no Android NDK.

---

## 2. Instalação Rápida (Sem Compilação)

Se você deseja apenas instalar e jogar:

1. **Instale o APK:**
   - O APK pré-compilado já está disponível no repositório em `apk/dantes_inferno_arm64.apk`.
   - Instale no seu celular via ADB ou gerenciador de arquivos:
     ```bash
     adb install -r apk/dantes_inferno_arm64.apk
     ```
2. **Copie o arquivo ISO:**
   - Não é necessário extrair a ISO no computador! O aplicativo possui um instalador interno de partição XDVDFS.
   - Copie o seu arquivo `.iso` do Dante's Inferno (Xbox 360, ~7.4 GB) para o armazenamento interno do celular (por exemplo, na raiz `/sdcard/`):
     ```bash
     adb push "Dante's Inferno (USA, Europe) (En,Fr,Es).iso" /sdcard/
     ```
3. **Abra o Jogo:**
   - Na tela inicial (**TitleActivity**), clique em **Jogar** ou selecione o arquivo ISO através do botão de seleção.
   - O instalador embutido extrairá os arquivos do jogo automaticamente para a pasta de dados do aplicativo.

---

## 3. Código C++ Recompilado Incluso no Repositório

Diferente do fluxo tradicional do ReXGlue que exige instalar a CLI do ReXGlue e extrair o `default.xex` no PC:
- **Os 114 arquivos C++ gerados** (`generated/default/dantes_inferno_recomp.*.cpp`) e o `sources.cmake` já estão rastreados e sincronizados no repositório.
- **Patches de Setjmp e Fibers** já estão aplicados diretamente no código-fonte.
- **Vantagem:** Qualquer desenvolvedor pode clonar o projeto e compilar o APK diretamente pelo Android Studio ou terminal, sem precisar rodar `rexglue codegen` nem scripts de patch adicionais.

---

## 4. Compilando o Projeto (Para Desenvolvedores)

### Requisitos:
* **Android NDK:** r26b ou superior (`r26.1.10909125` testado com sucesso).
* **JDK:** Java 17.
* **Android SDK:** Build-Tools 34.

### Método A: Linha de Comando (Gradle)
```bash
cd android
./gradlew assembleRelease
```
O APK final será gerado em `android/app/build/outputs/apk/release/app-release.apk`.

### Método B: Android Studio
1. Abra a pasta `android/` no **Android Studio**.
2. Aguarde a sincronização do Gradle e do CMake.
3. Conecte o dispositivo via USB (com Depuração USB ativada).
4. Clique em **Run** ou compile via menu **Build > Build Bundle(s) / APK(s) > Build APK(s)**.

---

## 5. Tela de Configurações e Responsividade UI (AdrenoTools, Vulkan e Persistência)

O aplicativo conta com uma tela completa de configurações (`SettingsActivity`), projetada com arquitetura em 2 colunas landscape e totalmente adaptada para telas de alta densidade (como **Xiaomi 12 - 2400x1080 / 440 DPI**):

* **Design Empilhado Responsivo (Label + Campo):**
  - Em telas de alta resolução e DPI elevado, campos horizontais causavam quebra de texto vertical no título dos menus. Todos os seletores (Spinners) foram refatorados com layout empilhado (`Label` superior de largura total e campo dropdown inferior com fundo estilizado `bg_spinner_field`), eliminando quebras de palavras e espaçamentos vazios.
* **Persistência Tripla Garantida:**
  1. **SharedPreferences:** Salva preferências do usuário no app de forma permanente.
  2. **Geração Física de `dantes_inferno.toml`:** O aplicativo gera e atualiza fisicamente o arquivo TOML na pasta do jogo, lido na inicialização pelo ReXGlue (`rex::cvar::LoadConfig`).
  3. **Ponte JNI Direta (`nativeSetGraphicsConfig`):** Ao iniciar o jogo, os valores de resolução, VSync, upscaler e threads são enviados diretamente ao C++ (`dantes_main_android.cpp`), aplicando os cvars no runtime Vulkan sem depender apenas de arquivos em disco.
* **Controles Integrados:**
  - **Driver GPU:** Alternador Turnip / Qualcomm OEM, Modo Turbo (GPU Boost), instalador de `.zip` e status em tempo real.
  - **Controles Touch:** Ativar/desativar botões virtuais e ajuste de opacidade (25% a 100%).
  - **Estabilidade:** Desativação de logs em disco e depuração (elimina stutters de I/O e validação de GPU).
  - **Gráficos Vulkan:** Resolução (720p 1x até 1440p 2x), VSync, Upscaler (FXAA, CAS, FSR), Modo Vulkan Present (FIFO, Mailbox, Immediate) e Threads de Criação de Pipeline.
  - **Cache de Shaders:** Leitura do tamanho real em disco (MB) e botão de limpeza instantânea.

---

## 6. Correções de Estabilidade Aplicadas no Port Android

* **Orientação de Tela Travada em Landscape:**
  - Forçado em `MainActivity.java` através de `SCREEN_ORIENTATION_SENSOR_LANDSCAPE` e dica do SDL3 `SDL_HINT_ORIENTATIONS`, impedindo recreações acidentais da Activity ao girar o aparelho.
* **Correção de Memória Scudo (Android Heap):**
  - Ajustado em `src/rex_app.cpp` no método `OnDestroy()`: liberação segura (`.release()`) dos drawers Vulkan/ImGui para evitar o erro fatal `Scudo: invalid chunk state`.
* **Caminhos de Dados e Permissões:**
  - Configuração explícita de `user_data_root` e `cache_root` no armazenamento interno do app em `src/dantes_main_android.cpp`, prevenindo falhas de permissão (`Permission denied` em `/data/.local`).
* **Correção de Artefatos Verdes nas Cenas Iniciais (Vídeos VP6 FMV):**
  - O decodificador de cutscenes VP6 executa como código PPC re枝recompilado. A instrução `vpkuwus128` (*Vector Pack Unsigned Word Unsigned Saturate*) convertia as palavras de 32 bits para 16 bits usando loops manuais que sofriam de *in-place aliasing* de registradores (o registrador destino sobrescrevia a leitura das palavras subsequentes nos registradores fonte compartilhados, como em `vpkuwus128 v63,v61,v63`). Substituído por chamada atômica SIMDE `simde_mm_packus_epi32` com saturação `simde_mm_min_epu32(0xFFFF)` em `recomp.35.cpp` e `recomp.103.cpp`, e automatizado em `patches/generated/apply_generated_patches.py`.
* **Instalador XDVDFS Embutido:**
  - Módulos `src/dantes_iso_installer.cpp` e `.h` para montagem e extração direta de ISOs XGD2/XGD3 no Android.

---

## 7. Controles e Jogabilidade no Android

O port foi adaptado para oferecer duas formas de controle no Android:

### 1. Controles Virtuais na Tela (Touch Overlay)
* **Ativado por padrão no Android** (`show_touch_controls = true`).
* **Analógico Esquerdo:** Movimentação fluida do Dante em 360°.
* **Botões de Ação (Layout Xbox):**
  * **A:** Pulo / Pulo duplo.
  * **X:** Ataque Rápido (Foice da Morte).
  * **Y:** Ataque Pesado (Foice da Morte).
  * **B:** Cruz Sagrada (Ataque sagrado à distância).
* **Gatilhos e Ombros:**
  * **LT:** Bloqueio / Esquiva.
  * **RT:** Agarrar inimigos / Punição ou Absolvição.
  * **LB:** Modificador de Magia.
  * **RB:** Interagir / Ações de cenário.
* **Configurações:** A opacidade e o tamanho dos joysticks podem ser ajustados via cvars:
  * `touch_controls_opacity` (padrão: `0.65`)
  * `touch_stick_radius` (padrão: `80.0`)

### 2. Gamepads Físicos (Bluetooth ou USB-C)
* Conecte qualquer controle Bluetooth ou USB (Xbox Wireless Controller, PlayStation DualSense / DualShock 4, Gamesir, Razer Kishi).
* O backend SDL3 reconhece automaticamente os controles com mapeamento oficial 1:1 e suporte a vibração (rumble).

---

## 8. Dicas de Otimização e Desempenho


* **Escala de Resolução:** Por padrão, o jogo roda na resolução nativa do Xbox 360 (1280x720). Em SoCs topo de linha (Snapdragon 8 Gen 2 / Gen 3 / Dimensity 9300), a escala pode ser aumentada nas Configurações da Tela Título.
* **V-Sync:** O jogo foi projetado para rodar a 60 FPS com VSync ativo.

---

## 6. Suporte a Drivers Turnip via AdrenoTools (Qualcomm Snapdragon)

O projeto integra nativamente a biblioteca **AdrenoTools** com interceptação dinâmica da GOT (`dlopen` / `dlclose`), permitindo carregar drivers Mesa Turnip open-source (`vulkan.adreno.so`) em tempo de execução sem necessitar de root:

1. Abra o jogo na tela título e acesse **Configurações**.
2. Na seção **Driver Gráfico GPU (AdrenoTools)**, clique em **📁 Instalar Driver Turnip (.zip)...**.
3. Selecione qualquer pacote `.zip` de driver Turnip recente (como os releases compilados por Kimchi ou Mesa). O app descompacta automaticamente para a pasta interna `custom_driver/` e lê o `meta.json`.
4. Ative a chave **Carregar Driver Turnip (AdrenoTools)** e, opcionalmente, o **Modo Turbo (GPU Boost)** para travar clocks máximos da GPU.
5. Inicie o jogo. O `dantes_driver` interceptará todas as chamadas `dlopen("libvulkan.so")` do `librexruntime.so`, `librexgpu-xenos.so` e `libSDL3.so` e redirecionará para o handle do Turnip, garantindo renderização de texturas comprimidas BC (DXT1/3/5) sem falhas.
6. **Mecanismo de Proteção:** Se porventura um driver instável causar travamento no boot, o aplicativo detecta o crash e reverte automaticamente para o driver estável do sistema (Qualcomm OEM) na próxima inicialização.

