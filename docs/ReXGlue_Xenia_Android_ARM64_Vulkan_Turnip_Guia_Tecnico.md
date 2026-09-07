# ReXGlue + Xenia + Android ARM64 + Vulkan/Turnip
## Guia técnico de arquitetura, portabilidade, kernel, memória, GPU e boas práticas

**Versão:** 1.0 — setembro de 2026  
**Escopo:** Xbox 360/Xenon, Xenia, ReXGlue, Android ARM64, Vulkan, Mesa/Turnip, Adreno, KGSL, Linux/Android kernel, compilação e otimização.

---

## 1. Objetivo

Este documento reúne uma arquitetura de referência para portar e otimizar software derivado de Xenia/ReXGlue para Android ARM64, principalmente em aparelhos Qualcomm Snapdragon com GPU Adreno e Vulkan/Turnip.

O objetivo não é apenas “fazer compilar”. O objetivo correto é construir uma cadeia previsível:

```text
Xbox 360 PowerPC
        │
        ▼
Recompilação / Runtime
        │
        ▼
Código nativo ARM64
        │
        ├── Runtime Xbox 360
        ├── memória virtual
        ├── threads/sincronização
        ├── áudio
        └── GPU abstraction
                    │
                    ▼
                  Vulkan
                    │
                    ▼
              Android Loader
                    │
                    ▼
              Mesa / Turnip
                    │
                    ▼
               KGSL / kernel
                    │
                    ▼
                Adreno GPU
```

A principal regra de engenharia é separar os problemas por camada. Um erro no kernel não deve ser “corrigido” com um hack no renderer Vulkan, e um problema de pipeline Vulkan não deve ser mascarado com alterações arbitrárias no código do jogo.

---

# 2. Xenia e ReXGlue não são a mesma coisa

## Xenia

Xenia é um emulador de Xbox 360. O projeto implementa o ambiente necessário para executar software Xbox 360, incluindo CPU PowerPC, kernel Xbox, memória, GPU, áudio, entrada e filesystem.

O kernel do Xenia usa shims para implementar APIs do kernel Xbox no host. O loader encontra imports do jogo e os associa às implementações do host.

## ReXGlue

ReXGlue segue outro modelo.

Ele converte código PowerPC do Xbox 360 para C++ portátil e usa recompilação estática/AOT em vez de interpretar ou JIT-compilar as instruções PPC durante a execução.

Isso muda radicalmente o perfil de CPU:

```text
Xenia:
PPC guest
   ↓
JIT
   ↓
ARM64/x64 host code

ReXGlue:
PPC
   ↓
recompilação AOT
   ↓
C++
   ↓
Clang
   ↓
ARM64 native code
```

Consequência:

**ReXGlue pode remover uma grande parte do custo recorrente de tradução de instruções, mas não elimina o custo do runtime Xbox 360, memória, sincronização, GPU, áudio e APIs do sistema.**

---

# 3. Modelo mental correto

Um port Android de alta performance deve ser analisado em cinco grandes blocos:

```text
CPU
 ├── PPC → ARM64
 ├── chamadas de função
 ├── ABI
 ├── SIMD/NEON
 └── threads

MEMÓRIA
 ├── guest virtual memory
 ├── host virtual memory
 ├── page size
 ├── mappings
 └── GPU memory

GPU
 ├── Xbox 360 GPU model
 ├── shader translation
 ├── render targets
 ├── EDRAM emulação
 ├── Vulkan
 └── Turnip

KERNEL
 ├── threads
 ├── futex
 ├── mmap
 ├── file I/O
 ├── synchronization
 ├── KGSL
 └── scheduler

ANDROID
 ├── Bionic
 ├── linker
 ├── SurfaceFlinger
 ├── ANativeWindow
 ├── Vulkan loader
 ├── AHardwareBuffer
 └── app lifecycle
```

---

# 4. ARM64 no Android

A ABI relevante para Snapdragon moderno é:

```text
arm64-v8a
```

Ela utiliza AArch64 e permite usar NEON/Advanced SIMD.

### Regras importantes

Não assuma que código C/C++ escrito para x86 pode ser recompilado sem alterações.

Problemas comuns:

- ponteiros tratados como `uint32_t`;
- casts entre ponteiro e inteiro;
- dependência de endianness;
- dependência de alinhamento;
- assembly x86;
- intrinsics SSE/AVX;
- uso incorreto de atomics;
- estruturas com layout dependente do compilador;
- assumptions sobre tamanho de `long`;
- código que depende de página de 4 KB;
- uso de registradores reservados pelo Android.

No Android ARM64, `x18` possui uso reservado pela plataforma e não deve ser manipulado arbitrariamente.

---

# 5. PowerPC Xbox 360 → ARM64

O Xbox 360 utiliza uma CPU Xenon baseada em PowerPC.

A recompilação precisa preservar semântica, não apenas produzir código que compile.

Aspectos importantes:

### Endianness

PowerPC Xbox 360 utiliza big-endian em vários contextos do guest.

ARM64 Android é little-endian.

Portanto:

```text
Guest PPC
big endian
    ↓
byte swap / load-store helpers
    ↓
ARM64
little endian
```

Não espalhe byte swapping manual pelo projeto.

Prefira helpers centralizados:

```cpp
load_be16()
load_be32()
load_be64()

store_be16()
store_be32()
store_be64()
```

Isso reduz bugs e permite ao compilador otimizar caminhos conhecidos.

### Alinhamento

Nunca suponha que qualquer endereço pode ser acessado com um load nativo ARM64.

Use acessos seguros quando o guest permitir endereços desalinhados.

---

# 6. Recompilação AOT

O modelo ideal:

```text
XEX
 ↓
análise
 ↓
PowerPC functions
 ↓
IR/recompiler
 ↓
C++
 ↓
Clang/LLVM
 ↓
AArch64
```

O compilador deve receber o máximo possível de informação estática.

Boas práticas:

- `constexpr` quando possível;
- funções pequenas e previsíveis;
- evitar indireção desnecessária;
- preservar aliasing correto;
- evitar `volatile` sem necessidade;
- usar `restrict` somente quando semanticamente correto;
- usar intrinsics NEON apenas após medir;
- manter hot paths isolados.

---

# 7. ABI e FFI

Ao atravessar:

```text
C++
 ↔
Java/Kotlin
```

ou:

```text
C++
 ↔
JNI
```

minimize chamadas.

Uma chamada JNI por objeto ou por frame é uma arquitetura ruim.

Prefira:

```text
Java/Kotlin
    ↓
JNI
    ↓
Native Runtime
```

com interfaces grossas.

O loop principal deve permanecer no lado nativo.

---

# 8. Threads

Uma arquitetura recomendada:

```text
UI thread
   │
   ├── configuração
   └── lifecycle

Game thread
   │
   └── runtime Xbox

CPU worker threads
   │
   └── jobs/recompiled code

GPU submission thread
   │
   └── Vulkan

Shader worker
   │
   └── compilation/cache

Audio thread
   │
   └── mixer/output
```

Não crie dezenas de threads sem medir.

Em Snapdragon, o scheduler pode migrar threads entre clusters de CPU. A afinidade manual só deve ser usada quando benchmarks mostrarem benefício consistente.

---

# 9. Sincronização

Sincronização excessiva pode destruir performance.

Evite:

```text
CPU
 ↓
GPU submit
 ↓
wait
 ↓
CPU
 ↓
GPU
```

Prefira uma fila de trabalho:

```text
CPU producer
      ↓
command buffer
      ↓
GPU queue
      ↓
fence/timeline
      ↓
CPU continua trabalhando
```

Use:

- atomics;
- futex quando disponível;
- condition variables;
- semáforos;
- timeline semaphores Vulkan;
- `VK_KHR_synchronization2` quando disponível.

Não faça busy-wait longo:

```cpp
while (!done) {}
```

Isso desperdiça CPU e energia.

---

# 10. Futex e kernel

Futex é uma das bases de sincronização eficiente em Linux.

O futex2 introduz `futex_waitv()`, permitindo esperar em um conjunto de futexes.

Isso é particularmente interessante para runtimes que precisam representar primitivas de sincronização complexas.

Mas:

**não assuma que todo Android/kernel possui a mesma interface de futex.**

Sempre faça capability detection.

Exemplo conceitual:

```text
kernel suporta waitv?
        │
   ┌────┴────┐
   │         │
  sim       não
   │         │
futex2    fallback
```

Não faça:

```text
Android 12 = sempre X
Android 15 = sempre Y
```

O kernel do aparelho é parte importante da equação.

---

# 11. Android Vulkan

A cadeia Android moderna é:

```text
Application
    ↓
libvulkan.so
    ↓
Android Vulkan Loader
    ↓
Vulkan Driver
    ↓
Kernel GPU driver
    ↓
GPU
```

O loader do Android fornece a interface principal.

Em dispositivos modernos, o driver pode ser integrado de maneiras diferentes. A partir do Android 15, o loader também possui suporte a carregar driver Vulkan via APEX.

Isso é importante para distribuição e atualização da pilha gráfica, mas não significa que qualquer aplicativo possa simplesmente substituir o driver do sistema.

---

# 12. Turnip

Turnip é o driver Vulkan Mesa para GPUs Adreno compatíveis.

A documentação atual do Mesa descreve Turnip como um driver Vulkan 1.3 para Adreno 6xx, com suporte específico variando conforme o chip e a versão do Mesa.

Arquiteturalmente:

```text
Vulkan API
    ↓
Turnip
    ↓
Freedreno
    ↓
KGSL/kernel interface
    ↓
Adreno
```

Turnip elimina a dependência da implementação Vulkan proprietária Qualcomm em espaço de usuário, mas **não elimina a dependência do kernel/driver GPU do dispositivo**.

Esse detalhe é fundamental.

---

# 13. Turnip não substitui o kernel

A afirmação correta é:

> Turnip substitui a implementação Vulkan de user space, mas continua dependendo da infraestrutura de GPU do kernel.

Portanto:

```text
Turnip moderno
+
kernel antigo
```

não é equivalente a:

```text
Turnip moderno
+
kernel moderno
```

O kernel ainda controla:

- acesso à GPU;
- memória;
- MMU/IOMMU;
- command submission;
- sincronização;
- fences;
- GPU scheduling;
- power management;
- interface KGSL.

---

# 14. Adreno

A arquitetura Adreno moderna é UMA:

```text
CPU
 │
 ├──────────┐
 │          │
 ▼          ▼
RAM       GPU
```

CPU e GPU compartilham a memória física, embora caches, mapeamentos e propriedades de memória sejam diferentes.

Isso torna importantes:

- cache coherency;
- memory domains;
- buffer usage;
- alignment;
- mapping;
- synchronization.

A documentação Mesa descreve a arquitetura Freedreno/Adreno como predominantemente baseada em tile rendering, com possibilidade de usar GMEM ou renderização direta em system memory dependendo do hardware e do caminho utilizado.

---

# 15. Vulkan: pipeline correto

Um pipeline gráfico moderno pode ser pensado como:

```text
Guest GPU state
       ↓
state translator
       ↓
shader translator
       ↓
SPIR-V
       ↓
VkShaderModule
       ↓
pipeline state
       ↓
VkPipeline
       ↓
command buffer
       ↓
queue submission
       ↓
Turnip
       ↓
Adreno
```

Cada etapa pode virar gargalo.

---

# 16. Pipeline cache

Nunca trate pipeline cache como detalhe secundário em um emulador.

Primeira execução:

```text
shader
 ↓
compile
 ↓
pipeline
 ↓
cache
```

Execução posterior:

```text
shader/state
 ↓
cache lookup
 ↓
reuse
```

Um bom cache reduz:

- shader stutter;
- pipeline creation spikes;
- CPU usage;
- tempo de loading.

Requisitos:

- chave determinística;
- versionamento;
- identificação de GPU/driver;
- invalidação segura;
- escrita atômica;
- tolerância a corrupção;
- cache por versão do backend.

Nunca reutilize cegamente um cache criado por outro driver.

---

# 17. Shader cache

Pipeline cache e shader cache não são exatamente a mesma coisa.

Use camadas:

```text
Guest shader
   ↓
Translated IR
   ↓
SPIR-V
   ↓
driver compilation
   ↓
GPU executable
```

Sempre que possível, cacheie a parte que é estável e reproduzível.

Uma estratégia prática:

```text
game_id
+
shader_hash
+
translator_version
+
backend_version
+
GPU family
+
driver identifier
```

gera a chave.

---

# 18. Dynamic Rendering

Quando suportado pelo dispositivo/driver, `VK_KHR_dynamic_rendering` reduz a dependência de objetos de render pass criados previamente.

Isso pode simplificar um backend moderno.

Mas não introduza dynamic rendering apenas por ser moderno.

Teste:

```text
render pass path
VS
dynamic rendering path
```

e mantenha fallback quando necessário.

---

# 19. Synchronization2

`VK_KHR_synchronization2` fornece uma interface mais moderna para sincronização.

Para um emulador complexo, isso pode simplificar:

- pipeline barriers;
- stage masks;
- access masks;
- queue synchronization.

Mas a regra continua:

**menos sincronização necessária é melhor que sincronização mais moderna.**

---

# 20. Descriptor management

Evite criar e destruir descriptor sets continuamente.

Prefira:

```text
descriptor pools persistentes
+
ring allocators
+
recycling
```

Quando a extensão e o driver permitirem, recursos modernos de descriptor indexing podem simplificar o gerenciamento.

Mas verifique feature bits reais.

Nunca assuma:

```cpp
VK_TRUE
```

sem consultar:

```cpp
VkPhysicalDeviceFeatures
VkPhysicalDeviceVulkan13Features
```

e extensões.

---

# 21. Memory allocation

Não faça uma alocação Vulkan para cada pequeno recurso.

Prefira:

```text
Large GPU allocation
       ↓
suballocation
       ├── buffer A
       ├── buffer B
       ├── texture C
       └── texture D
```

Use um allocator especializado ou um sistema próprio cuidadosamente testado.

Controle:

- alignment;
- memory type;
- host visible;
- device local;
- coherent;
- cached;
- transient.

---

# 22. EDRAM do Xbox 360

Este é um dos grandes pontos específicos de Xbox 360.

A GPU Xbox 360 possui uma arquitetura com EDRAM que não deve ser simplesmente tratada como uma textura Vulkan comum.

Uma abstração possível:

```text
Xbox 360 render target
        ↓
EDRAM model
        ↓
resolve
        ↓
Vulkan image
```

O renderer precisa preservar:

- formato;
- MSAA;
- depth/stencil;
- resolve;
- tiling;
- operações de blend;
- leitura/escrita;
- sincronização.

Erros aqui produzem tanto:

```text
gráficos incorretos
```

quanto:

```text
performance ruim
```

---

# 23. ROV / fragment shader interlock

Xenia historicamente possui requisitos gráficos relacionados a operações que dependem de ordenação de acesso a fragmentos.

GPUs que não possuem mecanismos adequados podem apresentar:

- menor performance;
- mais complexidade;
- bugs gráficos.

Em Adreno + Turnip, a disponibilidade real deve ser verificada através das extensões/features Vulkan expostas pelo driver.

Não force uma extensão que não existe.

---

# 24. Android 12 vs Android 15

A comparação correta não é:

```text
Android 12 = Vulkan ruim
Android 15 = Vulkan rápido
```

A realidade é:

```text
Android
+
Vulkan loader
+
driver user-space
+
kernel
+
GPU
+
runtime
```

No Android 15 há mudanças relevantes na infraestrutura.

Uma delas é suporte do AOSP a sistemas com páginas de 16 KB.

---

# 25. 4 KB vs 16 KB

Historicamente:

```text
Android
4 KB pages
```

Android 15 introduziu suporte AOSP para:

```text
16 KB pages
```

em configurações ARM64 compatíveis.

Isso afeta profundamente software nativo.

Problemas comuns:

```cpp
ptr & ~0xFFF
```

assume 4 KB.

Isso é ruim.

Prefira:

```cpp
page_size = sysconf(_SC_PAGESIZE);
```

ou mecanismos equivalentes apropriados.

Também evite:

```cpp
#define PAGE_SIZE 4096
```

quando o código pretende suportar ambientes de 16 KB.

---

# 26. ELF e alinhamento

Bibliotecas nativas também precisam ser preparadas para ambientes de 16 KB.

Um app com NDK pode exigir rebuild das bibliotecas nativas para suportar corretamente dispositivos 16 KB.

Para projetos grandes:

```text
libxenia.so
librexruntime.so
libvulkan_backend.so
libgame.so
```

devem ser verificadas individualmente.

---

# 27. Kernel Android

Um port sério deve separar:

```text
Android framework
Bionic
HAL
vendor
kernel
```

O fato de o sistema ser Android 15 não significa que todos os kernels sejam iguais.

Pode existir:

```text
Android 15
+
kernel 5.10
```

ou:

```text
Android 15
+
kernel 6.1
```

e eles podem apresentar diferenças importantes.

---

# 28. KGSL

Em Qualcomm, o KGSL é uma interface importante entre software e GPU.

O modelo conceitual:

```text
Mesa/Turnip
     ↓
GPU userspace interface
     ↓
KGSL
     ↓
GPU hardware
```

Por isso, diagnosticar um problema Vulkan deve incluir:

```text
Vulkan features
+
driver version
+
Mesa version
+
kernel version
+
KGSL information
+
GPU model
```

---

# 29. Diagnóstico obrigatório

Antes de otimizar, registre:

```text
uname -a
getprop ro.build.version.release
getprop ro.build.version.sdk
getprop ro.hardware
getprop ro.board.platform
getprop ro.product.cpu.abi
```

E, pelo Vulkan:

```text
Vulkan API version
driver name
driver version
device name
vendor ID
device ID
extensions
features
limits
memory heaps
memory types
```

Para Adreno:

```text
GPU family
GPU revision
Turnip version
Mesa version
kernel/KGSL version
```

---

# 30. Benchmark correto

Não use somente FPS.

Meça:

```text
CPU frame time
GPU frame time
frame time p50
frame time p95
frame time p99
shader compilation time
pipeline creation time
queue submit count
command buffer count
memory usage
GPU frequency
CPU frequency
thermal state
```

Exemplo:

```text
Average FPS: 60
p99 frame time: 55 ms
```

pode ser muito pior que:

```text
Average FPS: 58
p99 frame time: 19 ms
```

para percepção de fluidez.

---

# 31. Stutter

Classifique stutter:

### Tipo A — shader compilation

```text
CPU spike
shader compile
frame drop
```

### Tipo B — pipeline creation

```text
vkCreateGraphicsPipelines
↓
CPU spike
```

### Tipo C — GPU starvation

```text
GPU idle
CPU atrasada
```

### Tipo D — CPU bottleneck

```text
CPU 100%
GPU baixa utilização
```

### Tipo E — synchronization

```text
CPU
 ↓ wait
GPU
```

### Tipo F — thermal throttling

```text
temperatura ↑
frequency ↓
FPS ↓
```

Cada um precisa de uma solução diferente.

---

# 32. Android SurfaceFlinger

Mesmo que o emulador esteja renderizando corretamente, o frame ainda passa pela composição Android.

Fluxo simplificado:

```text
Vulkan
  ↓
ANativeWindow
  ↓
BufferQueue
  ↓
SurfaceFlinger
  ↓
Hardware Composer
  ↓
Display
```

Um backend Vulkan deve evitar cópias desnecessárias e sincronização excessiva com a apresentação.

---

# 33. Frame pacing

Não confunda:

```text
GPU rendering FPS
```

com:

```text
display presentation
```

Um emulador pode produzir 60 frames/s e ainda apresentar jitter.

O ideal é coordenar:

```text
game timing
+
GPU completion
+
display vsync
```

sem bloquear a CPU em cada frame.

---

# 34. Audio

Não use áudio como parte do frame loop.

Ruim:

```text
render frame
 ↓
mix audio
 ↓
wait audio
 ↓
next frame
```

Melhor:

```text
Game
 ↓
audio ring buffer
 ↓
audio thread
 ↓
Android audio API
```

---

# 35. Filesystem

Android possui sandbox.

Não assuma:

```text
./content
./cache
```

como faria no Windows.

Separe:

```text
internal app data
external app-specific storage
SAF/document provider
cache
save data
shader cache
pipeline cache
```

Não misture save com cache.

Cache pode ser apagado.

Save não.

---

# 36. Build recomendado

Para Android ARM64:

```text
CMake
+
Ninja
+
Clang
+
Android NDK
+
Gradle
```

Arquitetura:

```text
app/
native/
  runtime/
  gpu/
  vulkan/
  memory/
  kernel/
  audio/
  input/
game/
```

O runtime deve ser uma biblioteca nativa reutilizável.

---

# 37. CMake

Use targets separados:

```cmake
add_library(rexruntime STATIC ...)
add_library(rexgpu STATIC ...)
add_library(rexvulkan STATIC ...)
add_library(game SHARED ...)
```

Evite um único `CMakeLists.txt` gigante.

Use:

```text
INTERFACE libraries
PRIVATE dependencies
PUBLIC dependencies
target_compile_features
target_compile_definitions
```

---

# 38. NDK

Compile inicialmente para:

```text
arm64-v8a
```

Não introduza armeabi-v7a sem uma razão específica.

Isso reduz:

- código condicional;
- testes;
- complexidade;
- superfície de bugs.

---

# 39. Flags de compilação

Prioridade:

```text
-O2
```

depois:

```text
-O3
```

somente após benchmark.

Não use:

```text
-ffast-math
```

em código que exige comportamento IEEE preciso sem testar.

Para emulação, precisão geralmente é mais importante que micro-otimização.

---

# 40. NEON

NEON pode ser muito útil para:

- operações vetoriais;
- áudio;
- conversões;
- processamento de pixels;
- matemática.

Mas não transforme todo código em intrinsics.

Estratégia:

```text
C++ simples
 ↓
benchmark
 ↓
hotspot
 ↓
NEON
 ↓
benchmark novamente
```

---

# 41. Exemplo de arquitetura de projeto

```text
project/
├── app/
│   ├── AndroidManifest.xml
│   ├── MainActivity.kt
│   └── jni/
│
├── native/
│   ├── runtime/
│   ├── kernel/
│   ├── memory/
│   ├── cpu/
│   ├── gpu/
│   │   ├── vulkan/
│   │   ├── shader/
│   │   └── pipeline/
│   ├── audio/
│   └── platform/
│       └── android/
│
├── game/
│   ├── generated/
│   ├── patches/
│   └── data/
│
└── CMakeLists.txt
```

---

# 42. Feature detection

Nunca escreva:

```cpp
if (android_version >= 15)
```

para decidir se uma feature Vulkan existe.

Prefira:

```cpp
vkGetPhysicalDeviceFeatures2()
```

e verificar:

```text
feature
extension
limit
format support
queue support
memory support
```

Android 15 não garante que uma determinada GPU exponha todos os recursos opcionais.

---

# 43. Compatibilidade

Faça uma matriz:

| Recurso | Android 12 | Android 13 | Android 14 | Android 15 | Android 16 |
|---|---:|---:|---:|---:|---:|
| ARM64 | Sim | Sim | Sim | Sim | Sim |
| Vulkan 1.1 | comum | baseline moderno | obrigatório em Vulkan-capable | sim | sim |
| Vulkan 1.3 | depende do device | baseline de lançamento | amplamente esperado | comum | baseline para novos dispositivos |
| 16 KB pages | não como AOSP target | não | preparação | suportado | suportado |
| Turnip | depende da GPU/kernel | depende | depende | depende | depende |

Importante: “Android version” não substitui “GPU/driver capabilities”.

---

# 44. Android 13+ e Vulkan

A documentação atual do AOSP estabelece requisitos de versão Vulkan por geração de Android. Dispositivos que lançam com Android 13 ou superior precisam suportar Vulkan 1.3 nas condições descritas pelo CDD; Android 16 eleva o requisito de lançamento para Vulkan 1.4.

Para um projeto distribuído, portanto, vale separar:

```text
minimum API
```

de:

```text
minimum Vulkan feature set
```

---

# 45. ReXGlue + Android: prioridade de engenharia

Ordem recomendada:

## Fase 1 — Boot

```text
APK
 ↓
native library
 ↓
Runtime
 ↓
game
```

## Fase 2 — CPU

```text
PPC recompilado
 ↓
ARM64
 ↓
threading
```

## Fase 3 — memória

```text
guest memory
 ↓
host mappings
```

## Fase 4 — Vulkan

```text
device
 ↓
queue
 ↓
swapchain
 ↓
basic draw
```

## Fase 5 — GPU Xbox

```text
registers
 ↓
draw state
 ↓
shader
 ↓
render target
```

## Fase 6 — otimização

```text
cache
 ↓
pipeline
 ↓
sync
 ↓
memory
 ↓
profiling
```

---

# 46. O maior erro: otimizar cedo

Não faça:

```text
"Vamos colocar NEON"
"Vamos fixar CPU affinity"
"Vamos usar threads"
"Vamos mexer no kernel"
```

antes de medir.

Primeiro determine:

```text
CPU-bound?
GPU-bound?
memory-bound?
sync-bound?
shader-bound?
thermal-bound?
```

---

# 47. Ferramentas

Recomendadas:

### Android

```text
adb
logcat
perfetto
simpleperf
dumpsys
```

### Vulkan

```text
validation layers
RenderDoc quando compatível
Vulkan loader diagnostics
GPU vendor tools
```

### Linux/desktop

```text
perf
gdb
lldb
strace
```

### Mesa

```text
MESA_DEBUG
Mesa shader/cache diagnostics
Turnip/Freedreno debug facilities
```

Use ferramentas de tracing em builds de desenvolvimento, não no perfil final de performance.

---

# 48. Logs

Crie categorias:

```text
CPU
GPU
VULKAN
SHADER
PIPELINE
MEMORY
KERNEL
AUDIO
INPUT
ANDROID
```

E níveis:

```text
ERROR
WARN
INFO
DEBUG
TRACE
```

Nunca deixe TRACE ligado permanentemente durante benchmark.

---

# 49. Diagnóstico Vulkan mínimo

Ao iniciar:

```text
GPU:
Adreno XXXXX

Driver:
Turnip X.Y

Mesa:
X.Y

Vulkan:
1.3.x

API:
Android

Kernel:
X.Y

Page size:
4096 / 16384

Features:
...

Extensions:
...
```

Isso torna bugs reproduzíveis.

---

# 50. Reprodutibilidade

Um benchmark sério precisa registrar:

```text
device
SoC
GPU
Android
kernel
Mesa
Turnip
game build
ReXGlue commit
compiler
NDK
Vulkan features
resolution
frame limiter
thermal state
```

Sem isso, “ganhei 10 FPS” não significa muito.

---

# 51. Segurança

Não execute código externo arbitrário com privilégios.

Não dependa de:

```text
root
SELinux permissive
system modification
```

para o funcionamento normal do app.

Se root for necessário para testes de driver, trate como ambiente de desenvolvimento separado.

---

# 52. Kernel customizado

Kernel customizado pode ajudar, mas deve ser a última camada a modificar.

Primeiro:

```text
ReXGlue
 ↓
Vulkan backend
 ↓
Turnip
```

Depois:

```text
kernel configuration
```

Somente quando houver evidência de que o kernel é o gargalo.

Exemplos de áreas:

- scheduler;
- futex;
- memory mapping;
- KGSL;
- GPU frequency;
- thermal;
- power management.

Alterações erradas podem reduzir estabilidade e aumentar consumo.

---

# 53. Thermal throttling

Em celular:

```text
performance
      ↓
temperatura
      ↓
thermal governor
      ↓
frequency reduction
      ↓
FPS
```

Portanto, benchmark de 30 segundos pode enganar.

Faça:

```text
5 min
10 min
20 min
```

e compare a estabilidade.

---

# 54. GPU frequency

Não compare somente:

```text
GPU utilization = 90%
```

Utilização alta pode significar que o renderer está trabalhando corretamente.

Mas também pode significar que um shader está extremamente caro.

Registre:

```text
GPU busy
GPU frequency
GPU temperature
frame time
```

juntos.

---

# 55. CPU/GPU overlap

Objetivo:

```text
CPU:
[Game][Game][Game][Game]

GPU:
     [Render][Render][Render][Render]
```

Não:

```text
CPU:
[Game][WAIT][Game][WAIT]

GPU:
      [Render]      [Render]
```

O segundo cenário indica sincronização ruim.

---

# 56. Vulkan command buffers

Não grave comandos desnecessariamente.

Uma arquitetura eficiente pode usar:

```text
per-frame command pool
+
command buffers recicláveis
```

Evite destruir/criar pools todo frame.

---

# 57. Ring buffers

Excelente para recursos temporários:

```text
Frame N
 ├── uniforms
 ├── staging
 └── descriptors

Frame N+1
 ├── uniforms
 ├── staging
 └── descriptors
```

Use múltiplos segmentos para evitar sobrescrever dados ainda usados pela GPU.

---

# 58. Staging

Para upload:

```text
CPU
 ↓
staging buffer
 ↓
GPU copy
 ↓
device-local resource
```

Não faça `vkMapMemory`/`vkUnmapMemory` indiscriminadamente para cada pequena transferência.

---

# 59. Texturas

Agrupe uploads.

Ruim:

```text
texture 1 upload
submit
texture 2 upload
submit
texture 3 upload
submit
```

Melhor:

```text
batch uploads
 ↓
single command buffer
 ↓
single/few submissions
```

---

# 60. Shader translation

O translator Xbox → Vulkan deve ser determinístico.

Ideal:

```text
guest shader hash
       ↓
IR
       ↓
SPIR-V
```

O SPIR-V gerado deve ser estável para permitir caching.

---

# 61. Precisão vs performance

Em emulação:

```text
correto
```

vem antes de:

```text
rápido
```

Uma otimização que muda:

- floating point;
- NaN;
- denormals;
- rounding;
- integer overflow;
- synchronization;

pode causar bugs difíceis de rastrear.

Use otimizações graduais.

---

# 62. Estratégia para Adreno + Turnip

Para um Snapdragon moderno:

### Prioridade 1

```text
Turnip atualizado
```

### Prioridade 2

```text
Vulkan feature detection
```

### Prioridade 3

```text
pipeline/shader cache
```

### Prioridade 4

```text
redução de submits
```

### Prioridade 5

```text
memory allocation
```

### Prioridade 6

```text
synchronization
```

### Prioridade 7

```text
CPU/NEON optimization
```

### Prioridade 8

```text
kernel tuning
```

---

# 63. O que não fazer

Não:

- assumir Vulkan 1.3 apenas por ser Android 15;
- assumir que Turnip elimina todos os problemas de driver;
- assumir que Android 15 significa kernel novo;
- assumir páginas de 4 KB;
- hardcodar endereços;
- hardcodar `PAGE_SIZE=4096`;
- bloquear CPU esperando GPU sem necessidade;
- criar pipeline a cada draw;
- compilar shader repetidamente;
- criar milhares de threads;
- modificar kernel sem benchmark;
- misturar save com cache;
- usar hacks de precisão sem testes.

---

# 64. Checklist de produção

## CPU

- [ ] ARM64 puro
- [ ] Sem dependência SSE/AVX
- [ ] NEON somente em hotspots
- [ ] ABI correta
- [ ] Endianness correta
- [ ] Alinhamento correto

## Memória

- [ ] Guest memory isolada
- [ ] Host mappings corretos
- [ ] Sem PAGE_SIZE hardcoded
- [ ] 4 KB testado
- [ ] 16 KB testado
- [ ] Allocator eficiente

## Vulkan

- [ ] Feature detection
- [ ] Extension detection
- [ ] Pipeline cache
- [ ] Shader cache
- [ ] Descriptor recycling
- [ ] Command pool recycling
- [ ] Barriers corretas
- [ ] Timeline synchronization quando apropriado

## Android

- [ ] arm64-v8a
- [ ] NDK atualizado
- [ ] Native libraries compatíveis
- [ ] Scoped storage
- [ ] Lifecycle correto
- [ ] ANativeWindow
- [ ] Surface recreation
- [ ] 16 KB compatibility

## GPU

- [ ] Adreno identificado
- [ ] Turnip identificado
- [ ] Mesa identificado
- [ ] Vulkan version registrada
- [ ] Extensions registradas
- [ ] Thermal test
- [ ] Long-run benchmark

## Kernel

- [ ] versão registrada
- [ ] page size registrada
- [ ] KGSL identificado
- [ ] futex capabilities
- [ ] memory mapping
- [ ] GPU scheduler
- [ ] thermal governor

---

# 65. Arquitetura final recomendada

```text
┌───────────────────────────────────────────────┐
│                  Android App                  │
├───────────────────────────────────────────────┤
│ Kotlin/Java UI                                │
├───────────────────────────────────────────────┤
│ JNI / Native bridge                           │
├───────────────────────────────────────────────┤
│               ReXGlue Runtime                 │
│                                               │
│  Xbox Kernel ─ Memory ─ Threads ─ VFS ─ APU  │
├───────────────────────────────────────────────┤
│              Recompiled Game                  │
│              PowerPC → ARM64                  │
├───────────────────────────────────────────────┤
│              Xbox GPU Layer                   │
│  Registers / Shaders / EDRAM / Textures      │
├───────────────────────────────────────────────┤
│                 Vulkan                       │
│ Pipeline / Descriptor / Sync / Command       │
├───────────────────────────────────────────────┤
│              Android Loader                   │
├───────────────────────────────────────────────┤
│             Mesa / Turnip                    │
├───────────────────────────────────────────────┤
│              KGSL / Kernel                   │
├───────────────────────────────────────────────┤
│               Adreno GPU                     │
└───────────────────────────────────────────────┘
```

---

# 66. Conclusão

A arquitetura mais promissora para Xbox 360 recompilado em Android é:

```text
PowerPC
   ↓
AOT recompilation
   ↓
ARM64
   ↓
ReXGlue Runtime
   ↓
Vulkan backend
   ↓
Turnip
   ↓
KGSL
   ↓
Adreno
```

O ganho potencial do ReXGlue vem principalmente de remover o custo de tradução dinâmica de CPU.

O ganho do Vulkan vem de reduzir overhead e permitir controle explícito de:

- memória;
- pipelines;
- shaders;
- sincronização;
- command buffers.

O Turnip fornece uma implementação Vulkan open-source moderna para Adreno compatível, mas continua dependendo da infraestrutura do kernel.

E o Android 15 adiciona mudanças importantes para um port nativo moderno, incluindo suporte AOSP a configurações de páginas de 16 KB e mudanças no carregamento do driver Vulkan. Para novos dispositivos Android 16, o requisito de Vulkan de lançamento também sobe para 1.4.

Portanto, a arquitetura de maior qualidade não é simplesmente:

```text
Android 15 + Turnip = rápido
```

mas:

```text
ARM64 eficiente
+
recompilação correta
+
runtime enxuto
+
memória correta
+
Vulkan bem projetado
+
shader/pipeline cache
+
sincronização mínima
+
Turnip adequado
+
kernel compatível
+
controle térmico
=
alto desempenho sustentado
```

---

# 67. Fontes técnicas principais

- Android Open Source Project — Vulkan Architecture
- Android Open Source Project — Implement Vulkan
- Android Developers — ABIs
- Android Open Source Project — 16 KB page size
- Mesa — Freedreno/Turnip documentation
- Xenia — kernel documentation
- Xenia — Vulkan command processor
- Xenia — Android build infrastructure
- Xenia — ARM64 work/issues
- ReXGlue SDK — Runtime Architecture
- ReXGlue SDK — project documentation
- Linux Kernel — futex2 documentation

## Links oficiais

AOSP Vulkan:
https://source.android.com/docs/core/graphics/arch-vulkan

AOSP Implement Vulkan:
https://source.android.com/docs/core/graphics/implement-vulkan

Android 16 KB pages:
https://source.android.com/docs/core/architecture/16kb-page-size/16kb

Android ABI:
https://developer.android.com/ndk/guides/abis

Mesa Freedreno/Turnip:
https://docs.mesa3d.org/drivers/freedreno.html

Xenia:
https://github.com/xenia-project/xenia

Xenia kernel:
https://github.com/xenia-project/xenia/blob/master/docs/kernel.md

ReXGlue:
https://github.com/rexglue/rexglue-sdk

ReXGlue Runtime Architecture:
https://github.com/rexglue/rexglue-sdk/wiki/Runtime-Architecture-Overview

Linux futex2:
https://docs.kernel.org/userspace-api/futex2.html
