# UZDoom 3DS: estado (leer al empezar cada conversación)

Es el único estado válido del port. Se actualiza al cerrar cada prueba: la tabla, "Lo que sabemos" y "Pregunta abierta". Lo escribe quien trabaja (Catequilla o Claude), en español, corto.

**Rama de trabajo:** `uzdoom-3ds-test25` (parte de `catequilla-test24`, que guarda las pruebas 17 a 24 tal como quedaron).
**Compilar:** `platform/3ds/docker-build-test.sh NN` en Ubuntu, con devkitPro dentro de Docker (su instalador responde 403); con devkitPro instalado, `build-test.sh NN`. Ver README.
**Pruebas guardadas:** `~/work/3ds-ports/pruebas/NN/` (`.3dsx`, `.elf`, `info.txt`).
**Historial:** las pruebas cerradas, con sus resultados completos, están en `STATUS-historial.md`. No hace falta leerlo al empezar.

## Lo que sabemos (con la prueba que lo mostró)

1. El motor arranca entero y llega a `Init complete` (pruebas 7 y 13). La imagen de carga es de 640x480 para que quepa en memoria (13).
2. El gestor del joystick de SDL está vacío en la 3DS: `I_Rumble` lo comprueba (11-12). Los controles van por `hidScanInput()` en `I_StartTic` (33).
3. **Cuelgue de GPU arreglado:** el shader tiene que escribir todo `outtc0` (`mov outtc0.zw, ones`, `c3d_main.v.pica`) (29, 30).
4. **Imagen derecha:** las texturas se suben de abajo arriba (`sy = h - 1 - y*h/ph`, `c3d_hwtexture.cpp`) (31, 32).
5. **HOME funciona:** `I_StartFrame` llama a `aptMainLoop()`; HOME suspende y al volver el juego sigue (33). Se juega MAP01 → MAP02 y guardar partida funciona (34).
6. Los logs son fiables: `fsync` tras cada marca (25); prefijo `[T+ms heap=KB]` (31).
7. Carga ~97 s, de ellos ~52 s en `Texman.Init` (31, 32).
8. **Cierre desde HOME:** la orden llega (`aptMainLoop()` = false → `gameloop_abort`) y el juego se para **dentro de `D_Display()`**: sale `Before D_Display` y no `D_Display done` (35, 36). Tras volver de HOME el juego dibuja con normalidad (Alex, 60 s jugando), y libctru recupera la GPU al despertar (`hechos-3ds.md` S9).
9. **Menú:** sale grande y repetido arriba y abajo. El HUD, la consola `~` y la pantalla de título se ven bien (Alex, en persona, 13 nov.). Los valores de escala del motor son normales (34).
10. **OPTIONS cierra el juego:** `svcBreak` desde `GPUCMD_AddInternal` (resuelto con `addr2line` sobre la 36): el búfer de comandos de la GPU se llena (`hechos-3ds.md` F8).
11. **`C3D_FrameBegin` bloquea al cerrar desde HOME.** Con `C3D_FRAME_SYNCDRAW | C3D_FRAME_NONBLOCK` se queda indefinidamente en el fotograma 2016. La marca `FB_before` aparece pero no `FB_after` (prueba 37).
12. Sin `SYNCDRAW`, `C3D_FrameBegin` vuelve al cerrar desde HOME (`FB_after` en log A). `D_Cleanup()` completa todas sus funciones sin fallar (prueba 39). En la pasada 38B (saltando `D_Display()`) llega a `GameMain exit 0` pero hay crash post-main (dump PC=0, LR=2) (prueba 38).
14. **`C3D_RenderTargetDelete` bloquea al cerrar desde HOME** cuando hay trabajo GPU pendiente (prueba 40). Sin dibujar tras `gameloop_abort` el cierre llega a `GameMain exit 0` pero crash post-main (PC=0, LR=2) (prueba 38B).
15. **Cierre desde HOME arreglado:** sin dibujar tras `gameloop_abort` (prueba 41) y sin `romfsExit()` (prueba 42), el juego cierra completo: `D_Cleanup()`, post-cleanup, `SDL_Quit`, `gfxExit`, `atexit` y vuelta al HOME Menu sin crash. `romfsExit()` desregistraba el devoptab de "romfs:" mientras Newlib aún tenía FILE* abiertos a archivos .pk3 de romfs:, causando `_lseek_r` con FAR=0x18 (puntero nulo + offset de `seek_r`).
13. El automapa no dibuja líneas: el backend aún no dibuja líneas ni puntos.

## Pregunta abierta

**¿Por qué el menú sale grande y repetido?** También pendiente: cierre lento y música del HOME cortándose al cerrar.

## Registro de pruebas

| Prueba | Pregunta | Resultado |
|---|---|---|
| 8 | Primera imagen | Se cerró: `I_Rumble` con el joystick vacío (visto en la 11–12) |
| 9–10 | Memoria al arrancar | OOM de la imagen de carga (`malloc(4915200)`) |
| 11–12 | Cierre en `S_ClearSoundData` | Arreglado: comprobar el gestor del joystick en `I_Rumble` |
| 13 | Imagen de carga de 640x480 | Llega a `Init complete`; luego pantallas negras y consola colgada |
| 14–15 | ¿Dónde se para? | Dentro de `C3D_FrameBegin`, en el segundo render de la pantalla de carga |
| 16 | `FrameBegin` sin bloquear | `FB_out=1` en el primer fotograma, 0 en los demás |
| 17 | Interruptores y padding | 1.ª versión inválida (ELF renombrado). 2.ª: `GPU HANG` confirmado; interruptores **nunca probados** |
| 18 | ¿Se ejecuta `FrameEnd`? | Sí, en el fotograma 0 |
| 19 | Tiempos y espera P3D | Inválida: compilada con archivos viejos |
| 19b | Espera P3D | Se queda en `gspWaitForP3D`: no concluyente (`hechos-3ds.md`, F10) |
| 20 | Sondeo con `FrameBegin` | El log se corta en el arranque (¿apagado temprano?) |
| 21–22 | Saltar el fotograma 0 | El fotograma 1 se cuelga igual (mal interpretado; ver "Lo que sabemos", 6) |
| 23 | Comprobación de salud de la GPU al iniciar | Rompió el arranque; 23b sin datos nuevos |
| 24 | Buffer de comandos normal y limpieza | Log cortado en las fuentes; sin marcadores: no aporta |
| 25 | ¿Qué parte del fotograma cuelga? | H1 confirmada (dibujo cuelga, pasada B). H3 descartada (sin textura igual cuelga, D). H4 descartada (triángulo mínimo cuelga, C). Causa: shader o estado compartido. |
| 26 | ¿Shader del motor vs shader mínimo? | Shader del motor cuelga (log B). Shader mínimo funciona (log A). **El shader del motor es el culpable.** |
| 27 | ¿Qué aspecto del shader/datos causa el cuelgue? | **`outtc0` es la causa.** V1 (sin outtc0) OK, V2 (con outtc0) HANG, V3 (sin outtc0) OK, V4 (datos del motor, sin outtc0) OK. H3 y H4 descartadas. |
| 28 | ¿Por qué `outtc0` cuelga? ¿Es la declaración, `dp4`, o falta de textura? | **`dp4` escribiendo `outtc0` es la causa.** V0 (constante) OK, V1 (dp4) HANG, V2 (dp4+textura) HANG. Hipótesis A y C descartadas, B confirmada. |
| 29 | ¿Es la fuente de dp4 (`r2` vs `intex`) o faltar `outtc0.zw`? | **Faltar `outtc0.zw` es la causa.** V0 (sin zw) HANG, V1 repetida (con zw) OK, V2 (con zw) OK, V3 (sin zw) HANG. Todas con juego real. Hipótesis A, C, D descartadas, B confirmada. Error: la 1.ª V1 dio log incompleto y se usó para concluir; corregido con la repetición. |
| 30 | ¿El arreglo permanente (`mov outtc0.zw, ones`) funciona sin archivos de control? ¿Dónde está el cuello de botella? | ✅ Arreglo confirmado permanente. Juego arranca, muestra título y créditos. Imagen volteada verticalmente. Sin GPU HANG. Timestamp T+26331644 ms era un bug de formato `%lld` en Printf (arreglado para la 31). |
| 31 | ¿El volteo vertical está en la geometría o en las texturas? | ✅ Geometría OK: Y crece hacia abajo correctamente (V0 con testflip). Volteo está en texturas. Texman.Init es el cuello de botella (~51 s, 53%). Total carga ~97 s. |
| 32 | ¿El flip de filas en `CreateTexture` corrige la imagen? | ✅ Sí. Imagen perfecta: logo arriba, créditos en orden, letras derechas. Texman.Init ~52 s (53.4%). HOME sigue sin responder. |
| 33 | HOME y controles | ✅ HOME suspende y vuelve; los controles funcionan; se juega MAP01 → MAP02 |
| 34 | Cierre desde HOME y valores del menú | Llega la orden de cerrar (`[EXIT]`) y se atasca después; guardar partida funciona; valores de escala del menú normales |
| 35 | ¿Dónde se para el cierre desde HOME? | ✅ Entre `TryRunTics done` y `D_Display done`: `I_StartTic()`, `D_ProcessEvents()` o `D_Display()`. Hipótesis C descartada (el bucle llega a la iteración). |
| 36 | ¿Cuál de las 3 funciones se cuelga? | ✅ `D_Display()` es la culpable. `Before D_Display` presente, `D_Display done` ausente. I_StartTic y D_ProcessEvents completan sin problemas. |
| 37 | ¿Qué paso de `D_Display()` se para al cerrar desde HOME? | ✅ `C3D_FrameBegin` bloquea indefinidamente (f2016, con NONBLOCK). Hipótesis S confirmada, A1/B/C descartadas. |
| 38 | ¿Por qué `C3D_FrameBegin` no vuelve? | ✅ Sin SYNCDRAW vuelve (`FB_after`). Pero `D_Cleanup` se rompe: A se para en `D_Cleanup start`, B (sin D_Display) llega a `GameMain exit 0` pero Luma crasha post-main |
| 39 | ¿Dónde se para dentro de `D_Cleanup()`? | ✅ 35/35 marcas presentes. `D_Cleanup()` completa sin fallar. El problema está **después**, en el código post-`D_Cleanup()` de `GameMain` o en destructores globales |
| 40 | ¿Dónde se para post-`D_Cleanup()`? | ✅ `C3D_RenderTargetDelete(mTop)` bloquea. `mFrameStarted` false → saltó `C3D_FrameEnd`. GPU con trabajo pendiente. `C3D_Fini()` descartada como causa. |
| 41 | ¿Saltar `D_Display()` siempre con `gameloop_abort` arregla el cierre y dónde está el crash post-main? | ✅ Cierre completo: todas las marcas `[EXIT]` presentes hasta `atexit callback`. Crash ARM11 post-main en `_lseek_r`←`__libc_lock_release` (Newlib), FAR=0x18, Translation fault. Hipótesis A confirmada, B descartada. |
| 42 | ¿Quitar `romfsExit()` de `main()` evita el crash post-main? | ✅ Sí. Cierre completo, vuelta al HOME Menu sin crash. Todas las marcas `[EXIT]` presentes hasta `atexit callback`. `GameMain exit 0`. Hipótesis A confirmada, B descartada. Causa: `romfsExit()` desregistraba el devoptab de "romfs:" mientras Newlib tenía FILE* abiertos a archivos .pk3 de romfs:. |
| 90 | ¿`romfsInit()` falla en Azahar? | ✅ RomFS funciona: `init=OK`, `pk3: YES`. `doom2.wad` está presente y `stat()` lo encuentra. El motor no lo reconoce como IWAD válido. |
| 91 | ¿Dónde falla el reconocimiento del IWAD en Azahar? | ❌ Ninguna hipótesis acertó. `beforefilter count=0`: candidatos vacíos antes del filtro. Pista: `iwadparm=(null)`. S12: en Azahar `argc=0`, `-iwad` queda en la posición 0 de Args, `FArgs::CheckParm` busca desde la 1 y no lo ve. |

## Prueba 43

**Lo que se pidió:**
- [x] Medir por fotograma (con número de frame): llamadas a Draw2D, valores de viewport y scissor (antes y después de la rotación en `c3d_renderstate.cpp`), tamaño de F2DDrawer.
- [x] Detalles del primer draw command: vértices, tamaño de textura (original y subida), modo wrap, matriz transform (%.3f).
- [x] Marcas escasas, no cada frame.
- [x] Misma compilación probada en consola y Azahar; abrir menú con Start (`m`, Z8), captura con `azahar-captura.sh`.
- [x] Comparar logs línea a línea.

**Pregunta:** ¿Los valores de viewport, scissor y transform del menú encajan con el framebuffer de 240x400, o alguno está fuera de rango?

**Lo que ya sabemos:**
- Punto 9: menú grande y repetido en consola; HUD, título y consola OK.
- Punto 34: valores de escala normales.
- Z7: menú OK en Azahar (Software renderer).
- `c3d_renderstate.cpp:108-129`: SetScissor y SetViewport rotan coordenadas con `W=400`.
- `c3d_framebuffer.cpp:86`: framebuffer es 240x400.
- `hw_draw2d.cpp:54`: Draw2D llama SetViewport una vez al inicio, luego `hw_draw2d.cpp:132` llama SetScissor por command dentro del for.

**Hipótesis A:** La rotación de viewport/scissor es incorrecta para el menú (coordenadas fuera de rango del FB 240x400).
- Si A es cierta, el log mostrará `[C3D] menu vp` o `[C3D] menu sci` con valores negativos o > 400/>240 después de rotar.

**Hipótesis B:** El F2DDrawer o los vértices del menú usan dimensiones distintas a 400x240.
- Si B es cierta, el log mostrará `[C3D] menu 2dd` con Width/Height distintos de 400/240, o `[C3D] menu v0` con coordenadas de vértices fuera de [0,400]x[0,240].

**Hipótesis C:** La textura del menú se sube con dimensiones incorrectas o wrap mode no esperado.
- Si C es cierta, el log mostrará `[C3D] menu tex` con tamaño de textura subido distinto al original, o con wrap=CLAMP_NONE cuando `cmd.mFlags & DTF_Wrap` NO está activo (`hw_draw2d.cpp:175`: sin DTF_Wrap usa CLAMP_XY, con DTF_Wrap usa CLAMP_NONE).

**Única variable que cambia:** marcas de diagnóstico en `hw_draw2d.cpp:89-132` (bajo `#ifdef __3DS__`, bucle de commands dentro de Draw2D), `c3d_renderstate.cpp:108-129` (SetScissor, SetViewport) y `c3d_diag.h` + `c3d_diag.cpp:60-68` (nuevo switch `c3d-menu-diag.txt`, añadir `MenuDiag = Exists("c3d-menu-diag.txt")` en `Diag::Init()`). Solo se activan con el archivo de control. `hw_draw2d.cpp` es archivo común del motor: las marcas van bajo `#ifdef __3DS__`. Los archivos `c3d_*.cpp` usan `#include "menustate.h"` para acceder a `menuactive` (`menu.cpp:113`) y distinguir llamadas del menú de otros draw calls.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | menú como está (referencia visual) |
| B | `c3d-menu-diag.txt` (vacío) | menú igual, log con marcas `[C3D] menu` |

**Pasadas en Azahar:**
| Pasada | Qué hacer | Qué mirar |
|---|---|---|
| C | mismo build, `c3d-menu-diag.txt` vacío, abrir menú con `m` | captura con `azahar-captura.sh menu43`, log con marcas `[C3D] menu` |

**Comparar logs B (consola) y C (Azahar) línea a línea** para ver si viewport, scissor y transform son distintos entre hardware y emulador.

**Marcadores que se quedan:** los de la pregunta abierta (cierre desde HOME, `gameloop_abort`). **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 43`.

**Autocomprobación:**

1. **Tabla de diferencias.** Si cambias un shader, compara **instrucción por instrucción**: qué lee cada una (constante, uniforme, registro temporal, entrada) y **qué componentes escribe** (`.xy` no es lo mismo que `.xyzw`). Cada diferencia es una fila. Para cada par de pasadas que vas a comparar, haz una tabla con **todo** lo que cambia entre ellas: shader, valores de los uniformes, vértices (coordenadas y z), atributos (cuántos, cuáles fijos y qué registro), estado (blend, depth, combinadores), textura y llamada de dibujo. Para llenarla, lee el código de los **dos caminos**, no solo el que escribiste. Si cambia más de una fila, la prueba no aísla una sola variable: o separas las filas en pasadas distintas, o la conclusión solo podrá nombrar el conjunto ("el shader o sus datos").
   *Respuesta:* Entre A y B solo cambia que el log contiene marcas `[C3D] menu` (pasada B) o no (pasada A). Shader, uniformes, vértices, estado, textura y llamada de dibujo son idénticos. Entre B y C cambia solo la plataforma (hardware vs Azahar); el código es el mismo.

2. **¿Cada hipótesis predice algo distinto en el log?** Escribe la línea `[C3D]` concreta que saldría bajo cada una. Si dos hipótesis dan el mismo log, no las distingue esta prueba.
   *Respuesta:* A → `[C3D] menu vp x=... y=... w=... h=...` con valores negativos o >400/>240. B → `[C3D] menu 2dd W=... H=...` con W≠400 o H≠240. C → `[C3D] menu tex orig=... up=... wrap=...` con wrap≠CLAMP_XY. Si todas normales → el fallo es interpretación GPU, no valores.

3. **¿Lo que dices que se verá en pantalla puede verse?** Comprueba el color final con los datos reales: qué registro de entrada lee el shader y qué valor le llega. Un triángulo negro sobre fondo negro "no se ve" aunque funcione. Si la pantalla no es fiable, el criterio es el log.
   *Respuesta:* No hay cambio visual entre pasadas; el criterio es el log.

4. **¿Se resuelve en una sola compilación?** Si sabes de antemano que harán falta más pruebas para separar hipótesis ("para distinguir B de C haría falta la 28"), mete esas variantes **en esta**: un interruptor por variante, una pasada por variante. Cada compilación y cada ronda en la consola cuestan más que una pasada extra.
   *Respuesta:* Sí: si los valores son normales, la comparación B vs C (consola vs Azahar) ya está en el mismo build. Si algún valor es anómalo, se investiga ese valor directamente.

5. **¿Cada afirmación sobre una API tiene su fuente** (archivo y línea en `~/work/refs/`, o `hechos-3ds.md`)? No basta con que la función exista: escribe **qué dice ese código que pasa** y cuándo (por ejemplo, "el gancho ONEXIT se llama en `aptExit`, después de `main`"), y saca tu predicción del log de ahí, no de lo que suena lógico.
   *Respuesta:* `C3D_SetViewport` en `~/work/refs/citro3d/source/base.c:136` (firma `void C3D_SetViewport(u32 x, u32 y, u32 w, u32 h)`). `C3D_SetScissor` en `~/work/refs/citro3d/source/base.c:148` (firma `void C3D_SetScissor(GPU_SCISSORMODE mode, u32 left, u32 top, u32 right, u32 bottom)`). `F2DDrawer::GetWidth/Height` en `v_2ddrawer.h:265-266`. `RenderCommand::transform` en `v_2ddrawer.h:169`. `menuactive` declarado en `menustate.h:33`, definido en `menu.cpp:113`.

6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Busca la hipótesis que tu diseño no puede descartar porque esa fila de la tabla **nunca cambia** en ninguna pasada (por ejemplo, los datos del dibujo). Si la hay, añade una pasada que la cambie a ella sola, o escribe que esta prueba no la distingue.
   *Respuesta:* Si A, B y C dan valores normales en consola, sabría que los valores numéricos son correctos y el fallo está en cómo la GPU interpreta esos valores (estado residual, orden de llamadas, diferencia hardware vs Azahar). Comparando B vs C se ve si Azahar usa valores distintos.

7. **¿Puede Alex preparar cada pasada sin equivocarse?** Prefiere archivos de control vacíos con nombres distintos a archivos con contenido (un número dentro), que el Bloc de notas puede guardar con BOM o salto de línea.
   *Respuesta:* Sí: pasada A sin archivos, pasada B con `c3d-menu-diag.txt` vacío.

8. **¿Lo que vas a añadir ya existe?** Antes de crear una función, un archivo, un interruptor o una variable, busca su nombre: `grep -rn "nombre" src/` y mira si ese archivo se compila en la 3DS (`src/CMakeLists.txt`). Si ya existe, se modifica ahí (bajo `#ifdef __3DS__` si es código del motor); si defines otra, el enlazado falla.
   *Respuesta:* `grep -rn "MenuDiag" src/` → sin resultados. `Diag::Init()` y `Diag::Line()` ya existen en `c3d_diag.h/cpp`. Se añade `Diag::MenuDiag` como bool estático más en el mismo patrón.

9. **¿Qué se ejecuta después de tu cambio?** Sigue el flujo desde tu cambio hasta que el resultado se vea (en pantalla o en el log): qué funciones corren después, si alguna dibuja, espera o escribe en la SD, y si el log sigue abierto. Si en ese camino algo puede colgarse, pon una marca antes de ese paso.
   *Respuesta:* Tras cada marca `Draw2D()` sigue iterando commands (`hw_draw2d.cpp:89`). El log `N3D_Log` sigue abierto escribiendo a SD. No hay riesgo de cuelgue por las marcas.

## Pendientes que no son la pregunta abierta
- **OPTIONS:** el búfer de comandos de la GPU se llena (punto 10). Archivos en `~/work/3ds-ports/pruebas/37/crash-options-*`.
- **Cierre lento:** medir con los T+ms del log cuánto tarda desde `gameloop_abort set` hasta la última marca (prueba 42: T+102341 a T+130166 = ~28 s, pero incluye tiempo hasta el suspend).
- **Música del HOME:** se corta al cerrar el juego y tarda en volver (pruebas 41-42).
- Una línea del log tiene texto de otro idioma pegado antes de `Selecting OpenGL backend...`.
- Líneas y puntos (automapa), niebla, luz, portales, sonido, pantalla de abajo, `.cia` (unique ID 0xF3D5C).
- Las fuentes TrueType latinas generan atlas de 1 MB con todo el juego de caracteres: lento y caro en memoria. Recortarlo cuando el resto funcione.
