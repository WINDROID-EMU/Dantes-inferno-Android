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

## 2. Estrutura dos Arquivos do Jogo (~7.8GB)

Como um APK do Android não deve conter gigabytes de dados internamente, o jogo lê os assets a partir do armazenamento do aparelho.

1. Extraia a sua ISO do Dante's Inferno (Xbox 360) usando ferramentas como `extract-xiso` ou o `tools/asset_tool.py`.
2. Conecte o celular ao computador via USB.
3. Copie a pasta `game` extraída para um dos caminhos reconhecidos automaticamente pelo jogo:
   * `/sdcard/DantesInferno/game/`
   * Ou `/storage/emulated/0/DantesInferno/game/`
4. Certifique-se de que o arquivo executável principal está localizado exatamente em:
   ```text
   /sdcard/DantesInferno/game/default.xex
   /sdcard/DantesInferno/game/bigfile0.viv
   /sdcard/DantesInferno/game/bigfile1.viv
   ```

*Nota: Caso você coloque em outra pasta, o aplicativo abrirá um seletor de diretórios no primeiro início para que você indique a pasta.*

---

## 3. Compilando o Projeto

### Método A: Via Script Automatizado (Recomendado)

Defina a variável do NDK e execute o script:

```bash
export ANDROID_NDK_ROOT=$HOME/Android/Sdk/ndk/26.1.10909125
./scripts/build-android.sh
```

O script irá:
1. Compilar as bibliotecas nativas (`libdantes_inferno.so`, `librexruntime.so`, `librexgpu-xenos.so`) otimizadas para ARM64.
2. Acionar o Gradle para gerar o APK pronto em `android/app/build/outputs/apk/debug/app-debug.apk`.

### Método B: Via Android Studio

1. Abra o **Android Studio**.
2. Selecione **Open** e escolha a pasta `android/` deste repositório.
3. O Android Studio sincronizará o Gradle e detectará o `CMakeLists.txt` automaticamente.
4. Conecte seu dispositivo Android com depuração USB ativada e clique no botão **Run (Shift+F10)** ou selecione **Build > Build Bundle(s) / APK(s) > Build APK(s)**.

### Método C: Via CMake Presets

Se você deseja compilar apenas os binários `.so` nativos via linha de comando:

```bash
cmake --preset android-arm64-release -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake
cmake --build out/build/android-arm64-release --target dantes_inferno --parallel
```

---

## 4. Controles e Jogabilidade no Android

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

## 5. Dicas de Otimização e Desempenho

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

