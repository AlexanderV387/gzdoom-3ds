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
14. **`C3D_RenderTargetDelete` bloquea al cerrar desde HOME:** `mFrameStarted` era false (saltó `C3D_FrameEnd`), y `C3D_RenderTargetDelete(mTop)` se quedó en `C3Di_WaitAndClearQueue(-1)` esperando a la cola GPU (citro3d `renderqueue.c:381`, vía `C3D_RenderTargetDetachOutput` porque el target estaba enlazado). La GPU tenía trabajo pendiente sin cerrar (prueba 40).
13. El automapa no dibuja líneas: el backend aún no dibuja líneas ni puntos.

## Pregunta abierta

**¿Un "flush GPU" con timeout antes de `I_ShutdownGraphics()` vacía la cola y evita que `C3D_RenderTargetDelete` espere indefinidamente?** (prueba 41). También pendiente: el crash post-main y el menú.

## Prueba 41 (arreglo: vaciar la cola GPU antes de apagar)

**Lo que se pidió:**
- [x] Prueba 41 = arreglo, no más marcas
- [x] Que tras la orden de cerrar no quede trabajo GPU sin terminar
- [x] Apoyarse en F12 y la pasada B de la 38
- [x] Solo texto, sin código (primero la plantilla)

**Pregunta:** ¿Esperar a que la GPU termine el trabajo pendiente (con timeout de 120 vblank) antes de `I_ShutdownGraphics()` hace que el cierre desde HOME complete sin bloquear?

**Lo que ya sabemos:**
- Punto 14 (prueba 40): `C3D_RenderTargetDelete(mTop)` bloquea en `C3Di_WaitAndClearQueue(-1)` (citro3d `renderqueue.c:381`).
- F12: `C3D_Fini()` llama a `C3Di_WaitAndClearQueue(-1)` (citro3d `base.c:343` → `renderqueue.c:130`). Espera sin límite.
- `C3D_FrameEnd(0)` NO espera a la GPU: solo envía comandos con `gxCmdQueueRun` (`renderqueue.c:240`) y devuelve. Si `inFrame` es false, retorna sin hacer nada (`renderqueue.c:200`).
- `C3D_FrameBegin(C3D_FRAME_SYNCDRAW)` espera a que termine el trabajo del frame anterior (`renderqueue.c:160-172`). Con `C3D_FRAME_NONBLOCK` devuelve false si la GPU sigue ocupada, sin bloquear.
- Prueba 38B: saltando `D_Display()` en el último frame, el cierre funciona porque no hay trabajo GPU pendiente.
- En el log 40, el último frame completado (347) llamó a `C3D_FrameEnd(0)` pero la GPU aún procesaba comandos cuando llegó el destructor.
- **Nota sobre el hook citro3d:** `C3D_Init()` registra `APTHOOK_ONSUSPEND` (`base.c:131`) que llama a `C3Di_RenderQueueWaitDone()` → `WaitAndClearQueue(-1)` + `DisableVBlank` (`base.c:44-45`). Este hook ya drena la cola antes de que `aptMainLoop()` retorne. Si completó, la cola está vacía y parada. Si el juego sigue enviando comandos (frames 347-348), la cola se reinicia con `gxCmdQueueRun` en `C3D_FrameEnd`. El estado de la cola al llegar al shutdown depende de si esos frames tardíos completaron su trabajo GPU.

**Hipótesis A (el loop garantiza cola vacía y el cierre completa):**
- Antes de `I_ShutdownGraphics()` en `GameMain()` (d_main.cpp:4501), se añade un loop: hasta 120 vblank, intentar `C3D_FrameBegin(C3D_FRAME_NONBLOCK)`; si acepta, hacer `C3D_FrameEnd(0)` y salir. Si `FrameBegin` acepta, la cola GPU estaba vacía (o se vació). `FrameEnd` envía un frame dummy mínimo y cierra `inFrame`. Luego `C3D_RenderTargetDelete` llega con cola vacía y `inFrame=false`, y `WaitAndClearQueue(-1)` devuelve rápido.
- Si A es cierta: el log muestra `[EXIT] GM after I_ShutdownGraphics` y `[EXIT] FB ~after C3D_RenderTargetDelete`. El juego cierra.

**Hipótesis B (la GPU no termina nunca, haya o no timeout):**
- Si B es cierta: el loop agota los 120 vblank sin que `C3D_FrameBegin` acepte. O bien, `FrameBegin` acepta pero `FrameEnd` envía comandos que la GPU no procesa, y `C3D_RenderTargetDelete` igual espera. La última marca sigue siendo `[EXIT] FB ~before C3D_RenderTargetDelete`.

**Única variable que cambia:** el loop de "flush GPU" antes de `I_ShutdownGraphics()` en d_main.cpp.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| Única | ninguno | Log completo; juego cierra o cuelga |

**Marcadores que se quedan:** los `[EXIT]` de `D_Cleanup()` y post-`D_Cleanup()` (pruebas 39-40). Los `[EXIT] FB ~` del destructor se mantienen.
**Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 41`

**Autocomprobación:**
1. **Un cambio:** loop de flush GPU antes de `I_ShutdownGraphics()` en d_main.cpp. Sin tocar el renderizado ni el destructor.
2. **Hipótesis predicen distinto log:** A → `[EXIT] GM after I_ShutdownGraphics` presente. B → última marca `[EXIT] FB ~before C3D_RenderTargetDelete` (igual que la 40).
3. **Criterio es el log.**
4. **Una compilación, una pasada.**
5. **Código de citro3d leído:** `C3D_FrameBegin NONBLOCK` retorna false sin bloquear si GPU ocupada (`renderqueue.c:172`). `C3D_FrameEnd` envía y devuelve (`renderqueue.c:240`). `C3D_RenderTargetDelete` espera si `!inFrame` (`renderqueue.c:365-381`). El hook `APTHOOK_ONSUSPEND` (`base.c:44-45`) ya drena la cola; el loop de la prueba 41 es redundante si la cola ya está vacía, pero inoficioso.
6. **Si ambas fallan:** la GPU no responde ni con timeout. Falta investigar por qué los comandos no se procesan.
7. **Solo copiar `.3dsx`.**
8. **`C3D_FrameBegin`, `C3D_FrameEnd`, `gspWaitForVBlank` existen en citro3d/libctru, no se crea nada nuevo.**
9. **Después del loop:** si la cola está vacía, `I_ShutdownGraphics()` → destructor → `C3D_RenderTargetDelete` no espera. Si la cola sigue llena, el destructor igual espera en `C3D_RenderTargetDelete`.

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


## Pendientes que no son la pregunta abierta

- **Azahar:** la prueba 36 se cierra a los 6 s con "Cannot find a game IWAD", aunque `doom2.wad` está en la SD del emulador. Hipótesis: no se monta la RomFS del `.3dsx` (`romfsInit()` no se comprueba, `i_main_3ds.cpp:199`).
- **OPTIONS:** el búfer de comandos de la GPU se llena (punto 10). Archivos en `~/work/3ds-ports/pruebas/37/crash-options-*`.
- Una línea del log tiene texto de otro idioma pegado antes de `Selecting OpenGL backend...`.
- Líneas y puntos (automapa), niebla, luz, portales, sonido, pantalla de abajo, `.cia` (unique ID 0xF3D5C).
- Las fuentes TrueType latinas generan atlas de 1 MB con todo el juego de caracteres: lento y caro en memoria. Recortarlo cuando el resto funcione.
- **Crash post-main ARM11 (prueba 38B):** dump en luma/dumps/arm11, PC=0, LR=2, CPSR=0, Current process `3dsx_app`. Salto a dirección nula tras `main()`. Candidatos: gancho APT (el nuestro `[APT]`, `APTHOOK_ONEXIT` se llama en `aptExit` después de `main`, hecho S8), función `atexit`, destructor de objeto global. Investigar después de la 39.
