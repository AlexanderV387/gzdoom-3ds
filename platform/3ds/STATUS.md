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
14. **Menú: dos viewports alternan.** `Draw2D()` recibía `vp=640x400` (29 frames) y `vp=400x240` (15 frames). El 640x400 venía de `r_videoscale` forzando un mínimo de 640x400 para "special UI". Arreglado en la 47: viewport guard en `SetViewport()` y `r_videoscale` sin mínimo especial. Ahora el menú usa `vp=400x240` (prueba 47, l.71).
15. **OPTIONS y búfer de comandos GPU:** el crash (`svcBreak` en `GPUCMD_AddInternal`) era por búfer pequeño. Arreglado en la 47: búfer 4x más grande y salto de dibujo si se llena (prueba 47, Alex confirma OPTIONS abre sin crash).
16. **Cierre más rápido:** sin `fsync` en cada `[EXIT]` marca al cerrar, el cierre es más rápido (~34 s sin fsync vs ~28 s con fsync prueba 42, pero incluye el suspend de APT). El cierre completo funciona (prueba 47, l.84-87).
17. **Música del HOME:** no se corta al cerrar con los cambios de la 47 (Alex).

## Pregunta abierta

**¿Por qué la carga tarda ~94 s?** `Texman.Init` es el cuello de botella (~49 s, 52%). Las fuentes TrueType están desactivadas (prueba 47). También pendiente: bajones de FPS en partida y sin sonido (FluidSynth falló, fallback a ADL en l.55).

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
| 43 | ¿El menú manda a la GPU algo distinto que el HUD (geometría, textura, scissor)? | ✅ S descartada (scissor siempre `n=1 min=0,0 max=240,400`, l.67–11813+). ❌ G y T sin datos: sin líneas `geo`/`tex` en el log. Valores de escala normales. |
| 44 | ¿Capturamos `geo`/`tex` del menú subiendo límites y reseteando contadores? | ✅ Datos capturados (44 d2d, 44 geo, 252 tex). Dos viewports alternan: 400x240 y 640x400. Geometría excede 400x240 en ambos. far=0 siempre. Scissor cambia de portrait a landscape. |
| 49 | Mejoras de rendimiento y UX | ✅ Tres cambios funcionales. Pendiente: congelamientos con ADL. |
| 50 | Música en núcleo 1 | ✅ Sin congelamientos, pero la música se corta (núcleo 1 da ~30 % CPU). Log: `[AUDIO] StreamThread core=1 pri=0x30`. |
| 51 | Música en núcleo 2 | ✅ Música continua sin cortes en núcleo 2 (hecho H7). |
| 52 | Tiempos por categoría por imagen | ✅ Funciona. Valores en µs, worst acumulado, lógica mide TryRunTics entero, gu mezcla GPU y espera. |
| 53 | Refinar tiempos de la 52 | Pendiente. |

## Prueba 55

**Lo que se pidió:**
- [x] vid_vsync=false por defecto en 3DS (i_interface.cpp, patrón como vid_fps), sin quitar la opción de ponerlo a true en el ini.
- [x] Desglosar `ot` (~4,7 ms) en 4 campos separados:
  - `in`: I_StartTic + D_ProcessEvents (TC_Input).
  - `snd`: S_UpdateMusic (TC_Sound).
  - `pre`: D_Display antes de TC_Scene3D (TC_PreDisplay).
  - `fe`: C3D_FrameEnd (TC_FrameEnd).
- [x] Desglosar `hd` (~4,5 ms): HUD (DrawOverlays → TC_HDHud), envío a GPU (Draw2D → TC_HDGPU).
- [x] Contar llamadas 2D por imagen (d2).
- [x] Campos in, snd, pre, fe (avg/worst en ms) y d2 en [FPS]. Se elimina `ot`.
- [x] Nada más en esta prueba.

**Pregunta:** ¿El desglose de `ot` (in, snd, pre, fe) y `hd` (hdHud, hdGpu) identifica dónde se va el tiempo extra que impide los 60 fps?

**Lo que ya sabemos (con la prueba que lo mostró):**
- Punto 16: logs fiables con `fsync`.
- Prueba 54: sin vsync avg ~21 ms/imagen (~44 fps); `ot` ~4,7 ms, `hd` ~4,5 ms, `sc` ~8 ms, `lo` ~2 ms. Meta: 16,7 ms/imagen (60 fps).
- Prueba 53: tiempos en ms, worst por ventana, GPU real citro3d (gr/pr).
- i_interface.cpp:58 patrón `#ifdef __3DS__` para defaults de cvars. vid_vsync definido en v_video.cpp:185 (default false global).
- D_DoomLoop (d_main.cpp:1655-1673): I_StartFrame → TryRunTics → I_StartTic → D_ProcessEvents → D_Display → S_UpdateMusic.
- Hooks actuales: TC_Scene3D en d_main.cpp:1280-1579, TC_HUD2D en d_main.cpp:1580-1585.
- `ot` = avg − (lo + sc + hd): incluye I_StartTic, D_ProcessEvents, NetUpdate, S_UpdateMusic, D_Display pre-scene, C3D_FrameEnd, y overhead no medido.
- `hd` incluye DrawOverlays, FPS counter, Draw2D GPU send (c3d_framebuffer.cpp:275-280).

**Hipótesis A:** El overhead de `ot` se reparte entre input (in >1 ms), pre-scene (pre >1 ms) o FrameEnd (fe >1 ms); `hd` se va en envío GPU (hdGpu > hdHud).
- El log mostrará: uno o más de in/snd/pre/fe >1 ms avg, hdGpu > hdHud. Con vsync=false, avg baja pero no llega a 16,7 ms porque hay overhead significativo.
**Hipótesis B:** Los 4 campos de `ot` son bajos (<0,5 ms cada uno) y `hd` es bajo también; el cuello de botella es `sc` (~8 ms).
- El log mostrará: in, snd, pre, fe <0,5 ms cada uno; hdHud + hdGpu <3 ms. avg >17 ms porque `sc` consume el presupuesto. Habría que atacar `sc` en la siguiente prueba.

**Única variable que cambia:** c3d_diag.h, c3d_diag.cpp, d_main.cpp, c3d_framebuffer.cpp, bajo `#ifdef __3DS__`:
1. c3d_diag.h:51: añadir TC_Input, TC_Sound, TC_PreDisplay, TC_FrameEnd, TC_HDHud, TC_HDGPU a TimingCategory (TC_COUNT sube de 7 a 13).
2. c3d_diag.cpp:48-54: arrays TimingBeginTicks/Frame/Worst/Window ya usan TC_COUNT, se redimensionan solos.
3. d_main.cpp:1655-1673: TimingBegin/End(TC_Input) alrededor de I_StartTic + D_ProcessEvents; TimingBegin/End(TC_Sound) alrededor de S_UpdateMusic.
4. d_main.cpp:1257-1280: TimingBegin/End(TC_PreDisplay) alrededor de D_Display pre-scene (líneas 1257-1278, antes de TC_Scene3D).
5. d_main.cpp:1579-1585: TC_Scene3D end → TC_HDHud begin → DrawOverlays → TC_HDHud end → TC_HDGPU begin → End2DAndUpdate → TC_HDGPU end.
6. c3d_diag.cpp:144 [FPS]: eliminar `ot`, añadir `in=%.1f/%.1f snd=%.1f/%.1f pre=%.1f/%.1f fe=%.1f/%.1f d2=%d`. Contar 2D draws estático.
7. c3d_framebuffer.cpp:482: TimingBegin/End(TC_FrameEnd) alrededor de C3D_FrameEnd.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | Jugar MAP01 y MAP02 3 min; log con `[FPS]` con in, snd, pre, fe y d2 |

**Marcadores que se quedan:** los de la pregunta abierta. **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 55`.

**Autocomprobación:**
1. **Tabla de diferencias.** Una sola pasada A. Se añaden mediciones de timing (6 nuevas categorías) y se elimina `ot`; no cambia comportamiento del juego, solo diagnóstico. El [FPS] gana 5 campos (in, snd, pre, fe, d2). vid_vsync=false por defecto ya se probó en la 54B.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí: A muestra in/snd/pre/fe >1 ms avg o hdGpu > hdHud (componentes de ot y hd son el problema); B muestra los 4 campos <0,5 ms y hd <3 ms con sc ~8 ms dominante (sc es el problema).
3. **¿Lo que dices que se verá en pantalla puede verse?** El criterio es el log: `[FPS]` con in, snd, pre, fe y d2. La pantalla muestra gameplay normal a ~44 fps sin vsync.
4. **¿Se resuelve en una sola compilación?** Sí: una pasada A con MAP01 y MAP02, 3 min de gameplay. Los datos del log identifican qué componente es el cuello de botella.
5. **¿Cada afirmación sobre una API tiene su fuente?** TimingBegin/End en c3d_diag.h:52-53, TimingCategory enum en c3d_diag.h:51 (TC_COUNT sube a 13). vid_vsync en v_video.cpp:185 (CUSTOM_CVAR). Patrón `#ifdef __3DS__` CVAR en i_interface.cpp:58-62 (vid_fps). D_DoomLoop en d_main.cpp:1655-1673. Hooks Scene3D en d_main.cpp:1280 y 1579. Hooks HUD2D en d_main.cpp:1580-1585. Draw2D en c3d_framebuffer.cpp:275-280. C3D_FrameEnd en c3d_framebuffer.cpp:482.
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Con una pasada: si algún campo de ot >1 ms → A (ese componente es el problema de ot). Si hdGpu > hdHud → A (envío GPU es el problema en hd). Si todos <0,5 ms y sc ~8 ms → B (sc domina). Si todos bajos y avg ~16 ms → ya estamos cerca de 60 fps.
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí: una pasada, sin archivos de control.
8. **¿Lo que vas a añadir ya existe?** TimingBegin/End, TimingCategory, TimingWindow/Worst son variables existentes en c3d_diag.cpp. 6 nuevos valores del enum. TimingTicsWindow es variable existente. vid_vsync es cvar existente. Contador d2 es variable estática nueva. No se crea función ni archivo nuevo.
9. **¿Qué se ejecuta después de tu cambio?** Cada frame: I_StartFrame → TryRunTics (medido como TC_Logic) → I_StartTic + D_ProcessEvents (TC_Input) → D_Display pre-scene (TC_PreDisplay) → TC_Scene3D → TC_HDHud → TC_HDGPU → S_UpdateMusic (TC_Sound) → C3D_FrameEnd (TC_FrameEnd). [FPS] acumula y muestra los 4 campos de ot como avg/worst más d2=entero. El log sigue abierto (N3DS_Log en i_main_3ds.cpp:96).

## Prueba 53

**Lo que se pidió:**
- [x] Cambiar unidades de µs a ms en `[FPS]`: c3d_diag.cpp:136-148 divide ticks por 268.1 (ticks/µs); cambiar a `/ 268100.0` para ms, 1 decimal.
- [x] `TimingWorst` reset por ventana: c3d_diag.cpp:162-165 promueve `TimingFrame` → `TimingWorst` y limpia `TimingFrame` cada frame; añadir `TimingWorst[i] = 0` junto con `TimingWindow[i] = 0` en c3d_diag.cpp:152 (reset cada 5 s).
- [x] Medir solo el trabajo real de lógica: los hooks actuales envuelven `TryRunTics` entero (d_net.cpp:2197, 2355) incluyendo espera; mover para envolver solo el bucle `while (runTics--) { G_Ticker(); ... }` (d_net.cpp:2321-2342); guardar tics ejecutados en variable estática y loguearlos.
- [x] Tiempos reales GPU/CPU de citro3d: `gu` mezcla trabajo GPU y espera de refresh; leer `C3D_GetDrawingTime()` (GPU, ms) y `C3D_GetProcessingTime()` (CPU, ms) en `FrameDone`, citro3d renderqueue.c:249-256; `osTickCounterStart(&gpuTime)` en FrameEnd (l.239) solo pone `reference` sin borrar `elapsed` (libctru os.h:266-268), `onQueueFinish` actualiza `elapsed` cuando la GPU termina (l.46-49), y `osTickCounterRead` lee ese `elapsed` en ms (os.c:92-94), así que devuelve el tiempo GPU real del frame anterior ya completado.
- [x] Nada más en esta prueba.

**Pregunta:** ¿Los cuatro refinamientos muestran tiempos en ms con worst por ventana, lógica sin espera y GPU/CPU reales?

**Lo que ya sabemos (con la prueba que lo mostró):**
- Punto 16: logs fiables con `fsync`.
- Punto 17: carga ~97 s, Texman.Init ~52 s.
- Prueba 52: `[FPS]` funciona, valores en µs (ticks/268.1), worst acumulado toda la sesión, lógica mide TryRunTics entero (incluye espera), `gu` mezcla GPU y refresh.
- `osTickCounterRead` devuelve ms (libctru os.c:92-94: `elapsed / CPU_TICKS_PER_MSEC`).

**Hipótesis A:** Los cuatro refinamientos funcionan y el `[FPS]` muestra valores coherentes en ms.
- El log mostrará: worst reseteados por ventana (valores < 200 ms, no millones), lógica sin espera (valores bajos, < 5 ms), tics/imagen visible, GPU real de citro3d (ms del frame anterior completado) y CPU real de citro3d (ms de FrameBegin a FrameEnd) añadidos.
**Hipótesis B:** Algo rompe el cálculo o el formato.
- El log mostrará: valores anómalos (0, infinitos, negativos) o el formato `[FPS]` truncado/corrupto.

**Única variable que cambia:** `c3d_diag.cpp` y `d_net.cpp`, bajo `#ifdef __3DS__`:
1. c3d_diag.cpp:136-148: `/ 268.1` → `/ 268100.0` (ms en vez de µs).
2. c3d_diag.cpp:152: añadir `TimingWorst[i] = 0` dentro del reset de ventana (junto con `TimingWindow[i] = 0`). El promote-to-worst de l.162-165 se mantiene (promueve `TimingFrame` → `TimingWorst` y limpia `TimingFrame` cada frame).
3. c3d_diag.cpp:133: formato `[FPS]` añade `,ti=N,gr=%.1f,pr=%.1f` como últimos campos del `snprintf`, después de la última categoría `mu=`; `ti` es tics/imagen (entero), `gr` es GPU real citro3d (ms), `pr` es CPU real citro3d (ms). Búfer `buf[384]` ampliado a `buf[512]` si hace falta.
4. c3d_diag.cpp:47-51: declarar `static double TimingGPUWindow[TimingCats] = {};` y `static double TimingCPUWindow[TimingCats] = {};`. En `FrameDone`, leer `C3D_GetDrawingTime()` y `C3D_GetProcessingTime()` (devuelven ms del frame anterior completado) y añadirlos a esos acumuladores por ventana.
5. d_net.cpp:2197, 2355: quitar hooks TimingBegin/End de TryRunTics.
6. d_net.cpp:2321-2342: TimingBegin/End alrededor del `while (runTics--)`; guardar `runTics` original en variable estática `TimingTics` antes del bucle.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | Jugar MAP01-MAP02 5 min; log con `[FPS]` refinado |

**Marcadores que se quedan:** los de la pregunta abierta. **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 53`.

**Autocomprobación:**
1. **Tabla de diferencias.** Una sola pasada A. Solo se modifica cómo se calculan y formatean los valores del `[FPS]` y dónde se colocan los hooks de timing; no cambia comportamiento del juego, solo diagnóstico.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí: A muestra valores en ms coherentes (worst < 200 ms, lógica < 5 ms, tics visibles), B muestra valores anómalos o formato corrupto.
3. **¿Lo que dices que se verá en pantalla puede verse?** El criterio es el log: línea `[FPS]` con nuevos campos. La pantalla muestra gameplay normal.
4. **¿Se resuelve en una sola compilación?** Sí: una pasada A con 5 min de gameplay. Los datos del log confirman si los refinamientos funcionan.
5. **¿Cada afirmación sobre una API tiene su fuente?** `svcGetSystemTick` en libctru svc.h, `/ 268.1` en c3d_diag.cpp:136, `TimingWorst` en c3d_diag.cpp:49 y l.162-165, `TryRunTics` hooks en d_net.cpp:2197/2355, `G_Ticker` loop en d_net.cpp:2321-2342, `C3D_GetDrawingTime()`/`C3D_GetProcessingTime()` en citro3d renderqueue.c:249-256 (devuelven double ms vía `osTickCounterRead`, libctru os.c:92-94), `osTickCounterStart` solo pone `reference` sin borrar `elapsed` (libctru os.h:266-268), `onQueueFinish` actualiza `elapsed` (renderqueue.c:46-49).
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Con una pasada, si los valores son coherentes en ms → A. Si hay crash o valores anómalos → B. Si el formato se trunca → búfer de 384 bytes insuficiente.
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí: una pasada, sin archivos de control.
8. **¿Lo que vas a añadir ya existe?** `TimingWorst`, `TimingWindow`, `TimingFrame` son variables existentes en c3d_diag.cpp. `TimingGPUWindow` y `TimingCPUWindow` son nuevas variables estáticas declaradas en c3d_diag.cpp:47-51. `TimingTics` es nueva variable estática en d_net.cpp. `C3D_GetDrawingTime()` y `C3D_GetProcessingTime()` son de citro3d (incluidas por `<citro3d.h>` en c3d_diag.h:23). `runTics` es variable local existente en `TryRunTics`. No se crea función ni archivo nuevo.
9. **¿Qué se ejecuta después de tu cambio?** Cada frame: los hooks marcan inicio/fin de categorías (solo G_Ticker loop para lógica), `[FPS]` acumula y muestra medias/peores en ms, añade tics/imagen y GPU/CPU reales de citro3d. El log sigue abierto (N3DS_Log en i_main_3ds.cpp:96). El búfer `buf[384]` puede ser justo; se amplía si hace falta.

## Prueba 54

**Lo que se pidió:**
- [x] vid_vsync (ya existe como cvar, v_video.cpp:185) decide los flags de C3D_FrameBegin en la 3DS: true = SYNCDRAW|NONBLOCK como ahora; false = solo NONBLOCK (sin esperar al refresco).
- [x] Escribir en el log una vez el modo usado al iniciar.
- [x] En [FPS]: gr y pr como media por imagen (÷ frames), y un campo ot = avg − (lo + sc + hd) (resto: gu + tx + mu + overhead no medido).
- [x] Dos pasadas: A con vid_vsync=true, B con vid_vsync=false.
- [x] Nada más en esta prueba.

**Pregunta:** ¿Cuántos FPS da vid_vsync=false frente a true, y se ve tearing en pantalla?

**Lo que ya sabemos (con la prueba que lo mostró):**
- F2: con SYNCDRAW, C3D_FrameBegin espera a que pasen las vblank de las dos pantallas antes de nada (hechos-3ds.md, l.14).
- Datos de Alex (prueba 53 en ejecución): lógica ~1-3 ms, GPU real ~4-9 ms/imagen, trabajo total ~17-20 ms/imagen; gu ~13 ms parado, FPS pegados a 30.
- Punto 12: sin SYNCDRAW, C3D_FrameBegin vuelve de inmediato (prueba 39).
- vid_vsync es cvar existente (v_video.cpp:185), declarado EXTERN_CVAR en d_net.cpp:70.
- c3d_framebuffer.cpp:190 pone siempre SYNCDRAW|NONBLOCK; SetVSync es no-op (c3d_backend.h:215).

**Hipótesis A:** Sin SYNCDRAW el FPS sube a >45 fps (avg < 11 ms) y se ve tearing.
- El log mostrará: `[C3D] vsync mode=off` en la pasada B. `[FPS]` con gu < 2 ms media y avg < 11 ms. ot bajo (~1-3 ms, solo tx + mu sin espera GPU). Alex confirma tearing visual.
**Hipótesis B:** Sin SYNCDRAW el FPS sube pero no mucho (>35 pero <45 fps, avg 11-16 ms) o se queda en ~30.
- El log mostrará: `[C3D] vsync mode=off`. gu < 2 ms pero avg entre 11 y 16 ms (sc o hd consumen el presupuesto) o avg > 16.7 ms (sin cambio). Tearing leve o ausente.

**Única variable que cambia:** c3d_framebuffer.cpp y c3d_diag.cpp, bajo `#ifdef __3DS__`:
1. c3d_framebuffer.cpp:190: `vid_vsync` decide fbFlags (true → SYNCDRAW|NONBLOCK, false → NONBLOCK). `EXTERN_CVAR(Bool, vid_vsync)` añadido.
2. c3d_framebuffer.cpp: primer FrameBegun loguea `[C3D] vsync mode=on` o `off` una vez.
3. c3d_diag.cpp:139: `gr` y `pr` divididos por frames (media por imagen). Nuevo campo `ot=%.1f` = avg − (lo + sc + hd) en ms.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | `vid_vsync=true` en el ini | Jugar MAP01-MAP02 3 min; log con `[FPS]` y `[C3D] vsync mode=on`; sin tearing |
| B | `vid_vsync=false` en el ini | Jugar MAP01-MAP02 3 min; log con `[FPS]` y `[C3D] vsync mode=off`; ¿tearing? |

**Marcadores que se quedan:** los de la pregunta abierta. **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 54`.

**Autocomprobación:**
1. **Tabla de diferencias.** Una variable: fbFlags en C3D_FrameBegin. Pasada A: `SYNCDRAW|NONBLOCK`, pasada B: `NONBLOCK`. Todo lo demás igual (mismo shader, misma geometría, mismos draws). El `[FPS]` cambia formato (gr/pr por imagen, ot añadido) pero es idéntico en ambas pasadas.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí: A muestra avg < 11 ms en B (>45 fps) con tearing visible; B muestra avg 11-16 ms o >16.7 ms con tearing leve o ausente. Ambas muestran gu bajo en B.
3. **¿Lo que dices que se verá en pantalla puede verse?** El criterio es el log: `[C3D] vsync mode=on/off` y `[FPS]` con gu, avg, ot. La pantalla muestra gameplay normal.
4. **¿Se resuelve en una sola compilación?** Sí: dos pasadas A y B con el mismo `.3dsx`, solo cambia el ini.
5. **¿Cada afirmación sobre una API tiene su fuente?** `vid_vsync` en v_video.cpp:185 (CUSTOM_CVAR, Bool), EXTERN_CVAR en d_net.cpp:70. SYNCDRAW|NONBLOCK en c3d_framebuffer.cpp:190. F2 en hechos-3ds.md l.14: SYNCDRAW espera vblank de ambas pantallas. Sin SYNCDRAW vuelve de inmediato (prueba 39). SetVSync no-op en c3d_backend.h:215.
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Si gu baja en B pero avg no baja → B (sc o hd altos). Si gu y avg bajan → A. Si gu no baja → el cambio no surtió efecto (compilación incorrecta o vid_vsync no leído).
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí: solo cambiar `vid_vsync=true` o `false` en el ini.
8. **¿Lo que vas a añadir ya existe?** `vid_vsync` es cvar existente. `EXTERN_CVAR` patrón usado en d_net.cpp:70 y otros archivos. `N3DS_Log` ya usado en c3d_framebuffer.cpp (marcas `[EXIT]`). `TimingWindow`, `TimingWorst`, `TimingGPUWindow`, `TimingCPUWindow` son variables existentes en c3d_diag.cpp. No se crea función ni archivo nuevo.
9. **¿Qué se ejecuta después de tu cambio?** Cada frame: `screen->Update()` llama a `BeginFrame()` → `C3D_FrameBegin(fbFlags)` con flags según vid_vsync. FrameDone escribe `[FPS]` con gr/pr por imagen y ot. El log sigue abierto (N3DS_Log en i_main_3ds.cpp:96).

## Prueba 49

**Lo que se pidió:**
- [x] Arreglar `snd_backend` atado a "null": en 3DS, si es "null", ignorarlo y usar OpenAL siempre (i_sound.cpp:258-262). Sin marcador.
- [x] `gl_precache` por defecto `true` en 3DS (hw_cvars.cpp:138). El backend ya tiene `PrecacheMaterial` (c3d_framebuffer.cpp:483).
- [x] `vid_fps` por defecto `true` en 3DS (i_interface.cpp:58).

**Pregunta:** ¿Las tres mejoras funcionan sin romper el arranque ni el gameplay?

**Lo que ya sabemos:**
- S17: carga ~97 s, Texman.Init ~52 s. La 48 bajó a 19 s / 4.6 s respectivamente.
- La 48 sin sonido porque `snd_backend=null` se guardó en ini (CVAR_ARCHIVE, i_sound.cpp:63).
- OpenAL Soft ya se compila con la 48 (build.sh:26-29, `NO_OPENAL=OFF`).
- `PrecacheMaterial` existe en c3d_framebuffer.cpp:483.
- `I_InitSound()` (i_sound.cpp:244) lee `snd_backend` ya cargado del ini.

**Hipótesis A:** Las tres mejoras funcionan y el juego arranca y se juega con sonido, precache activo y FPS visible.
- El log mostrará: `S_Init: Setting up sound.` (d_main.cpp:3722) seguido de `Texman.Init done` y `Init complete` sin crash. FPS visible arriba a la derecha (d_main.cpp:1061). Sonido al jugar MAP01.
**Hipótesis B:** Algo rompe el arranque.
- El log se corta antes de `Init complete` o hay crash.

**Única variable que cambia:** tres archivos del motor bajo `#ifdef __3DS__`:
1. `i_sound.cpp:258-262`: en 3DS saltar el chequeo `"null"` e inicializar OpenAL siempre (sin marcador).
2. `hw_cvars.cpp:138`: `gl_precache` default `true` en 3DS.
3. `i_interface.cpp:58`: `vid_fps` default `true` en 3DS.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | FPS visible arriba a la derecha; sonido al jugar MAP01 |

**Marcadores que se quedan:** los de la pregunta abierta (~94 s de carga). **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 49`.

**Autocomprobación:**
1. **Tabla de diferencias.** No hay pasadas comparativas (una sola pasada A). Los tres cambios son defaults bajo `#ifdef __3DS__`; no hay shader ni estado GPU que comparar instrucción a instrucción.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí: A muestra logs completos hasta `Init complete` y gameplay; B se corta antes o da crash.
3. **¿Lo que dices que se verá en pantalla puede verse?** El FPS counter (`vid_fps`) dibuja texto blanco arriba a la derecha (d_main.cpp:1061). El sonido se oye al disparar/pisar. Precache es interno pero acelera la carga.
4. **¿Se resuelve en una sola compilación?** Sí: una pasada A con los tres cambios activos. Si uno falla, el log lo muestra.
5. **¿Cada afirmación sobre una API tiene su fuente?** `snd_backend` en i_sound.cpp:63 (CVAR), `I_InitSound` en i_sound.cpp:244, `gl_precache` en hw_cvars.cpp:138, `vid_fps` en i_interface.cpp:58, `PrecacheMaterial` en c3d_framebuffer.cpp:483, FPS draw en d_main.cpp:1061.
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Con una sola pasada, si hay crash se sabe que algo rompió (B). Si funciona, A se confirma. No hay fila que "nunca cambia".
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí: una sola pasada, sin archivos de control.
8. **¿Lo que vas a añadir ya existe?** `snd_backend`, `gl_precache` y `vid_fps` son CVARs existentes modificados con `#ifdef __3DS__`. No se crea función ni variable nueva. El marcador `snd_fixed.txt` es un archivo simple en la SD.
9. **¿Qué se ejecuta después de tu cambio?** `I_InitSound()` (i_sound.cpp:244) usa `snd_backend` para elegir el renderer. `gl_precache` se lee en hw_precache.cpp:260 al cargar texturas. `vid_fps` se lee cada frame en d_main.cpp:1052 para dibujar el HUD. El log sigue abierto (N3DS_Log en i_main_3ds.cpp:96).

## Prueba 50

**Lo que se pidió:**
- [x] Trasladar a UZDoom el arreglo del port de Legend of Doom para el hilo de música (hecho H4): en `CreateStream`, bajo `#ifdef __3DS__`, crear `StreamThread` con `threadCreate` en núcleo 1, pila 64 KB, prioridad del hilo principal limitada a [0x18, 0x3E]; fallback a núcleo 0 con misma prioridad.
- [x] `BufferCount = 8` en la 3DS (en vez de 4, oalsound.cpp:232).
- [x] Línea de log al crear el hilo: en qué núcleo y con qué prioridad quedó (hecho H5).
- [x] Añadir `#include <3ds.h>` bajo `#ifdef __3DS__` en oalsound.h para que `Thread` compile.
- [x] Nada más en esta prueba.

**Pregunta:** ¿Trasladar el hilo de música al núcleo 1 con prioridad corregida elimina los congelamientos de 2-8 s con ADL?

**Lo que ya sabemos (con la prueba que lo mostró):**
- H1: `std::thread` crea hilos en núcleo 0 con prioridad 0x3F (la más baja). Prueba 49: congelamientos 2-8 s con ADL.
- H3: ADL sintetiza música en `StreamThread` (oalsound.cpp:1267); con `std::thread` el hilo se estrella contra el núcleo 0 cargado.
- H4: Legend of Doom lo resolvió con `threadCreate` en núcleo 1 (main: oalsound.cpp:1320-1355).
- H5: No sabemos si `.3dsx` concede núcleo 1; hay que loguearlo.
- snd_mididevice=-7 selecciona ADL directamente (music.cpp:479).

**Hipótesis A:** El arreglo funciona. La música va al núcleo 1 con prioridad corregida, sin competir con el juego.
- El log mostrará: `I_InitSound: Initializing OpenAL` y `Init complete`, luego `[AUDIO] StreamThread core=N pri=0xXX` al arrancar la música (N=1 si concede, N=0 si fallback). Sin congelamientos en 5 min de partida.
**Hipótesis B:** El arreglo no elimina los congelamientos o rompe el arranque.
- El log se corta antes de `Init complete` (crash en crear el hilo) o hay congelamientos 2-8 s como en la 49.

**Única variable que cambia:** `src/common/audio/sound/oalsound.h` y `oalsound.cpp`, bajo `#ifdef __3DS__`:
1. oalsound.h:190: `Thread StreamThread = nullptr` (libctru) en vez de `std::thread`.
2. oalsound.cpp:925-931 (destructor): `threadJoin`/`threadFree` en vez de `.joinable()`/`.join()`.
3. oalsound.cpp:968+: wrapper C `BackgroundProc3DS` para que `threadCreate` llame al método C++ `BackgroundProc`.
4. oalsound.cpp:1264-1275 (`CreateStream`): `threadCreate` en núcleo 1 con fallback a núcleo 0, más log `[AUDIO] StreamThread core=N pri=0xXX`.
5. oalsound.cpp:232: `BufferCount = 8` en vez de 4.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | `snd_mididevice=-7` en el ini | Música sin congelamientos en MAP01-MAP02; log con `[AUDIO] StreamThread core=N pri=0xXX` |

**Marcadores que se quedan:** los de la pregunta abierta. **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 50`.

**Autocomprobación:**
1. **Tabla de diferencias.** No hay pasadas comparativas (una sola pasada A). El cambio es cómo se crea y gestiona el hilo: `std::thread` → `threadCreate` (libctru) en núcleo 1, fallback núcleo 0, `threadJoin`/`threadFree` en destructor. BufferCount 4 → 8.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí: A muestra log completo con `[AUDIO] StreamThread` y sin congelamientos; B muestra crash o congelamientos como la 49. Si `threadCreate` falla en ambos núcleos, el log muestra `Unable to create the 3DS music stream thread` y no hay música.
3. **¿Lo que dices que se verá en pantalla puede verse?** La música se oye al entrar en MAP01. Sin congelamientos = gameplay fluido. El criterio es el log y la experiencia de Alex.
4. **¿Se resuelve en una sola compilación?** Sí: una pasada A. Si hay congelamientos, la causa es otra.
5. **¿Cada afirmación sobre una API tiene su fuente?** `threadCreate` en libctru thread.h:51, `threadJoin`/`threadFree` en libctru, prioridad en H1, núcleo 1 en H4/H5, ADL en music.cpp:479, `BufferCount` en oalsound.cpp:232, wrapper en main: oalsound.cpp:1009-1013.
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Con una pasada, si hay crash → B (algo rompió). Si hay congelamientos → B (el arreglo no basta). Si `threadCreate` falla → H5 confirmado (`.3dsx` no concede núcleo 1 ni 0 con esa prioridad). Si funciona → A confirmada.
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí: una pasada, solo `snd_mididevice=-7` en el ini.
8. **¿Lo que vas a añadir ya existe?** `threadCreate`, `threadJoin`, `threadFree` son de libctru. `StreamThread` es variable existente en oalsound.h:190, solo cambia el tipo. `BackgroundProc3DS` es una función nueva (wrapper C) importada de la rama main (oalsound.cpp:1009). `BufferCount` es variable existente en oalsound.cpp:232.
9. **¿Qué se ejecuta después de tu cambio?** `CreateStream` se llama al iniciar el reproductor de música. `BackgroundProc` corre en el hilo nuevo (llamado por `BackgroundProc3DS`). El destructor usa `threadJoin`/`threadFree` al cerrar. El log sigue abierto (N3DS_Log en i_main_3ds.cpp:96).

## Prueba 52

**Lo que se pidió:**
- [x] Medir dónde se va el tiempo de cada imagen con `svcGetSystemTick` (268.1 MHz, libctru os.h:16-17, SYSCLOCK_ARM11).
- [x] Añadir al `[FPS]` de c3d_diag.cpp la media y el peor caso de 6 categorías:
  1. Lógica del juego (tics: `TryRunTics` en d_net.cpp:2190 → `G_Ticker` en g_game.cpp:1245 → `P_Ticker` en p_tick.cpp:168).
  2. Preparar escena 3D en CPU (`D_Display` en d_main.cpp:1256, renderizador hw antes de mandar dibujos).
  3. HUD y 2D (`DrawOverlays` en d_main.cpp:1092, `End2DAndUpdate` en d_main.cpp:1109 → `screen->Update()` en c3d_framebuffer.cpp:401).
  4. Espera GPU en `C3D_FrameBegin` (c3d_framebuffer.cpp:188-218): tiempo total en FrameBegin incluyendo reintentos; FrameBegin con NONBLOCK no espera pero los retries sí consumen tiempo cuando la GPU está saturada.
  5. Subidas de texturas: cuántas y cuánto tardan (`C3DHardwareTexture::CreateTexture` en c3d_hwtexture.cpp:63, `C3D_TexFlush` en c3d_hwtexture.cpp:117).
  6. Cambio de música (`S_ChangeMusic` en music.cpp:680).
- [x] No cambiar nada más.

**Pregunta:** ¿Los tiempos por categoría identifican qué parte hace que cada imagen tarde >16,7 ms y limite a 30-35 fps?

**Lo que ya sabemos (con la prueba que lo mostró):**
- Punto 16: logs fiables con `fsync` tras cada marca.
- Punto 17: carga ~97 s, Texman.Init ~52 s.
- FPS oscilan entre 30 y 35, promedio 33,4 ms/imagen (Alex).
- ~65 draws/imagen → 49-55 fps; ~100 draws → 30 fps.
- `svcGetSystemTick` a 26,8 MHz (libctru os.h:16).
- H7: música en núcleo 2, continua, sin cortes.

**Hipótesis A:** El tiempo extra viene de la GPU (espera en `FrameBegin`/`FrameEnd`) cuando hay muchos draws.
- El log `[FPS]` mostrará: media GPU >5 ms, peor caso GPU >16,7 ms, correlacionado con draws/imagen altos.
**Hipótesis B:** El tiempo extra viene de la CPU (lógica del juego o preparar escena 3D).
- El log `[FPS]` mostrará: media lógica o escena >5 ms, peor caso >16,7 ms.
**Hipótesis C:** El tiempo extra viene de texturas nuevas subidas cada imagen.
- El log `[FPS]` mostrará: texturas/subida significativas (>1 ms media), correlacionado con bajones de fps.

**Única variable que cambia:** `src/common/rendering/c3d/c3d_diag.h` y `c3d_diag.cpp`: variables de timing con `svcGetSystemTick`, funciones hook para marcar inicio/fin de cada categoría, y extensión de la línea `[FPS]` para mostrar medias y peores casos. Ganchos inyectados bajo `#ifdef __3DS__` en d_net.cpp, d_main.cpp, c3d_framebuffer.cpp, c3d_hwtexture.cpp y music.cpp.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | Jugar MAP01-MAP02 5 min; log con `[FPS]` extendido mostrando tiempos por categoría |

**Marcadores que se quedan:** los de la pregunta abierta. **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 52`.

**Autocomprobación:**
1. **Tabla de diferencias.** No hay pasadas comparativas (una sola pasada A). Se añaden mediciones de timing con `svcGetSystemTick` en 6 categorías; no se cambia comportamiento, solo se mide.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí: A muestra GPU como categoría más costosa (>16,7 ms worst), B muestra lógica/escena como más costosa, C muestra texturas como más costosa.
3. **¿Lo que dices que se verá en pantalla puede verse?** El criterio es el log: línea `[FPS]` extendida con tiempos por categoría. La pantalla muestra gameplay normal.
4. **¿Se resuelve en una sola compilación?** Sí: una pasada A con 5 min de gameplay en MAP01-MAP02. Los datos del log identifican la categoría más costosa.
5. **¿Cada afirmación sobre una API tiene su fuente?** `svcGetSystemTick` en libctru svc.h:1149, reloj a 26,8 MHz en os.h:16. `TryRunTics` en d_net.cpp:2190, `G_Ticker` en g_game.cpp:1245, `P_Ticker` en p_tick.cpp:168, `D_Display` en d_main.cpp:1256, `DrawOverlays` en d_main.cpp:1092, `End2DAndUpdate` en d_main.cpp:1109, `C3D_FrameBegin` en c3d_framebuffer.cpp:188, `C3D_FrameEnd` en c3d_framebuffer.cpp:463, `CreateTexture` en c3d_hwtexture.cpp:63, `C3D_TexFlush` en c3d_hwtexture.cpp:117, `S_ChangeMusic` en music.cpp:680.
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Si ninguna categoría supera significativamente >16,7 ms en worst, el cuello de botella está en otra parte no medida (overhead del motor, sincronización, o la suma de varias categorías). Si GPU domina → A. Si lógica/escena → B. Si texturas → C.
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí: una sola pasada, sin archivos de control.
8. **¿Lo que vas a añadir ya existe?** `svcGetSystemTick` es de libctru. `c3d_diag.h`/`.cpp` son archivos existentes del port. Las funciones hook son nuevas pero en namespace `C3DRenderer::Diag`, consistente con el patrón existente. Las variables de timing son estáticas en c3d_diag.cpp.
9. **¿Qué se ejecuta después de tu cambio?** Cada frame: los hooks marcan inicio/fin de cada categoría, el `[FPS]` acumula y muestra medias/peores. El log sigue abierto (N3DS_Log en i_main_3ds.cpp:96). Los hooks son llamadas ligeras (`svcGetSystemTick` + resta) que no afectan al rendimiento significativamente.

## Prueba 51

**Lo que se pidió:**
- [x] Cambiar el orden de núcleos del hilo de música: primero núcleo 2 (New 3DS), si falla llamar `APT_SetAppCpuTimeLimit(80)` y probar núcleo 1, si falla núcleo 0.
- [x] El log debe mostrar el núcleo, la prioridad y el CPU limit: `[AUDIO] StreamThread core=N pri=0xXX cpulimit=NN` (leído con `APT_GetAppCpuTimeLimit`).
- [x] Nada más en esta prueba.

**Pregunta:** ¿Usar el núcleo 2 para el hilo de música elimina los cortes de música con OPL?

**Lo que ya sabemos (con la prueba que lo mostró):**
- H6: el núcleo 2 funciona en `.3dsx` y da CPU completa; el núcleo 1 da ~30 % y corta la música (prueba 50).
- Prueba 50: sin congelamientos en núcleo 1, pero la música se corta (log `[AUDIO] StreamThread core=1 pri=0x30`).
- H2: `threadCreate` acepta núcleo 2 si el exheader lo permite (thread.h:20-44).
- `APT_SetAppCpuTimeLimit` sube el tiempo del núcleo 1 (services/apt.h:515, wolf4sdl-3ds patrón).

**Hipótesis A:** El núcleo 2 acepta el hilo y la música suena sin cortes.
- El log mostrará: `[AUDIO] StreamThread core=2 pri=0x30 cpulimit=NN`. Música continua en MAP01-MAP02.
**Hipótesis B:** El núcleo 2 no acepta el hilo y hace fallback al núcleo 1 (con `cpulimit=80`) o 0.
- El log mostrará: `[AUDIO] StreamThread core=1 pri=0x30 cpulimit=80` o `core=0`. Con núcleo 1 y 80 % la música mejora pero puede seguir cortando; con núcleo 0 compite con el juego.

**Única variable que cambia:** `src/common/audio/sound/oalsound.cpp`, función `CreateStream` (l.1299-1315): primer `threadCreate` con núcleo `2`, `APT_SetAppCpuTimeLimit(80)` si falla, y `cpulimit` en el log.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | `snd_mididevice=-3` en el ini | Música continua sin cortes en MAP01-MAP02; log con `[AUDIO] StreamThread core=N pri=0xXX cpulimit=NN` |

**Marcadores que se quedan:** los de la pregunta abierta. **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 51`.

**Autocomprobación:**
1. **Tabla de diferencias.** Una sola pasada A. Cambios en `CreateStream`: primer intento núcleo `2` (en vez de `1`), `APT_SetAppCpuTimeLimit(80)` si falla el núcleo 2, fallback núcleo 1 → 0. El log añade `cpulimit=NN` leído con `APT_GetAppCpuTimeLimit`. Prioridad y pila iguales.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí: A muestra `core=2`, B muestra `core=1 cpulimit=80` o `core=0`. La experiencia de Alex confirma si hay cortes.
3. **¿Lo que dices que se verá en pantalla puede verse?** La música continua sin cortes se oye en MAP01-MAP02. El criterio principal es el log (`core=2`) y la experiencia de Alex.
4. **¿Se resuelve en una sola compilación?** Sí: una pasada A. Si `core=2` y no hay cortes → A. Si falla y cae a `core=1` con `cpulimit=80` → B.
5. **¿Cada afirmación sobre una API tiene su fuente?** `threadCreate(func, arg, pila, prio, nucleo, detached)` en libctru thread.h:20-44 (H2): acepta el núcleo como parámetro; si el exheader no lo concede devuelve `nullptr`. `APT_SetAppCpuTimeLimit(u32 percent)` en services/apt.h:515, se llama si el núcleo 2 falla. `APT_GetAppCpuTimeLimit(u32 *percent)` en services/apt.h:521, se lee al final para el log. Se llama desde `CreateStream` (oalsound.cpp:1299) al iniciar la música. H6 confirma que el núcleo 2 funciona en `.3dsx` y da el orden 2→1→0.
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Si `core=2` y hay cortes → otra causa. Si `core=1` con `cpulimit=80` y mejora → el 80 % ayuda pero no basta. Si `core=0` → núcleo 2 no concedido ni 1 con 80%. Si crash → algo rompió.
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí: una pasada, solo `snd_mididevice=-3` en el ini.
8. **¿Lo que vas a añadir ya existe?** `threadCreate` es la misma llamada de la prueba 50. `APT_SetAppCpuTimeLimit` y `APT_GetAppCpuTimeLimit` son de libctru (`<3ds.h>` ya incluido en oalsound.h:29). No se crea función ni variable nueva.
9. **¿Qué se ejecuta después de tu cambio?** `CreateStream` se llama al iniciar la música. `BackgroundProc` corre en el hilo del núcleo asignado. El destructor usa `threadJoin`/`threadFree`. El log sigue abierto (N3DS_Log en i_main_3ds.cpp:96).

## Prueba 61

**Lo que se pidió:**
- [ ] **[WIPE]:** Encontrar por qué el efecto de wipe (wipetype=1) no se ve entre pantallas. Añadir marcas `[WIPE]` en los cambios de pantalla indicando: tipo de wipe, si `WipeStartScreen` y `WipeEndScreen` devuelven textura o `nullptr`, y si se dibuja algún paso del efecto. Citar líneas.
- [ ] **[LOAD]:** Analizar el arranque (~20 s hasta "Init complete"). Añadir marcas `[load] T+` en:
    - `M_LoadDefaults` $\to$ `W_Init` (1,6 s).
    - Carga de `doom2.wad` (~2,9 s). Comprobar si hay MD5.
    - `S_Init` $\to$ `I_Init` (2,2 s).
    - `Texman.Init` $\to$ `textures added` (4,8 s).
    - `LoadActors` (3,8 s).

**Lo que ya sabemos (con la prueba que lo mostró):**
- El wipe actual solo muestra un salto de pantalla sin efecto visual en 3DS.
- Los tiempos de arranque conocidos son: `M_LoadDefaults` $\to$ `W_Init` (1,6 s); añadir `doom2.wad` (~2,9 s); `S_Init` $\to$ `I_Init` (2,2 s); `Texman.Init` $\to$ `textures added` (4,8 s); `LoadActors` (3,8 s).

**Hipótesis A (WIPE):** Las funciones `WipeStartScreen`/`EndScreen` devuelven `nullptr` en 3DS o las texturas creadas no son válidas para el driver.
- Si A es cierta, el log mostrará: `[WIPE] type=1, start=nullptr, end=...` o similar.

**Pasadas en la consola** (cada una con su log; el dueño renombra log.txt a log-A.txt, log-B.txt...):
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | Comprobar si hay wipe visual y ver los tiempos de arranque en el log. |

**Marcadores que se quedan:** los de la pregunta abierta. **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 61`.

**Autocomprobación:**
1. **Tabla de diferencias.** N/A (es una sola pasada para recoger datos).
2. **¿Cada hipótesis predice algo distinto en el log?** Sí: A muestra `nullptr` o B muestra un tiempo largo en la carga del IWAD.
3. **¿Lo que dices que se verá en pantalla puede verse?** Sí, el wipe (o la falta de él) es visible.
4. **¿Se resuelve en una sola compilación?** Sí, para recoger los datos.
5. **¿Cada afirmación sobre una API tiene su fuente?** Se usará `grep` y lectura de archivos para las citas.
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Si no hay wipe y no hay error en texturas, la causa es el dibujo (`PerformWipe`).
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí.
8. **¿Lo que vas a añadir ya existe?** No, se añaden nuevas marcas de log.
9. **¿Qué se ejecuta después de tu cambio?** El motor arranca y llega al gameplay.
**Lo que se pidió:**
- [ ] Medir la iteración completa de `D_DoomLoop` (`src/d_main.cpp`) como "it" (al inicio y fin del `for(;;)` loop) para comprobar que `it ≈ avg`.
- [ ] Medir por separado en el bucle: `GC::CheckGC` (recolector de ZScript), `NetUpdate` y cualquier espera o `sleep` (`I_WaitForTic`, `I_WaitVBL`) como `TC_Wait` (citar con `grep`).
- [ ] Corregir `d2`: debe contar cada comando 2D que se dibuja dentro de `::Draw2D` (en el bucle de `src/common/rendering/hwrenderer/hw_draw2d.cpp`), no las imágenes.

**Pregunta:** ¿El desglose de la iteración (`it`) y los componentes individuales (`gc`, `net`, `wait`) junto con la corrección de `d2` (comandos 2D en lugar de imágenes) identifica el origen del tiempo extra?

**Lo que ya sabemos (con la prueba que lo mostró):**
- Prueba 55: `in/snd/pre/fe ≈ 0`, `hdH 0,3 ms`, `hdG ~4,6 ms` (envío 2D), y ~4,5 ms constantes sin medir.
- `D_DoomLoop` (d_main.cpp:1655-1673) incluye `I_StartFrame` → `TryRunTics` → `I_StartTic` → `D_ProcessEvents` → `D_Display` → `S_UpdateMusic`.
- Los hooks actuales miden `TC_Scene3D` y `TC_HUD2D`.
- `GC::CheckGC` se llama en `d_main.cpp:1263` y `d_net.cpp:2196`.
- `NetUpdate` se llama en `d_net.cpp:2225`.
- Los comandos 2D se envían en `c3d_framebuffer.cpp:280`.

**Hipótesis A:** El tiempo extra se debe a `GC::CheckGC` o `NetUpdate`, o a esperas no medidas (`wait`), o a un número muy alto de comandos 2D.
- El log mostrará: `it` significativamente mayor que `avg` (si `avg` no incluye todo), o alguno de `gc`, `net`, `wait` > 1 ms, o `d2` (comandos 2D) muy alto.

**Hipótesis B:** El tiempo extra se debe a la carga de la GPU o al procesamiento de la escena 3D (ya medidos en pruebas anteriores), y los nuevos campos (`gc`, `net`, `wait`, `it`) son bajos.
- El log mostrará: `it`, `gc`, `net`, `wait` bajos (< 0,5 ms), y `d2` (comandos 2D) similar a lo que se esperaba de las imágenes.

**Única variable que cambia:** `src/d_main.cpp`, `src/common/rendering/c3d/c3d_diag.h` y `src/common/rendering/c3d/c3d_diag.cpp`, `src/common/rendering/c3d/c3d_framebuffer.cpp` y `src/common/rendering/hwrenderer/hw_draw2d.cpp`, bajo `#ifdef __3DS__`:
1. `c3d_diag.h`: añadir `TC_Loop`, `TC_GC`, `TC_Net`, `TC_Wait` a `TimingCategory`.
2. `c3d_diag.cpp`: implementar las nuevas categorías y el formato `[FPS]` con `it, gc, net, wait, d2`.
3. `d_main.cpp`: añadir `TimingBegin/End` para `TC_Loop` en el `for(;;)` de `D_DoomLoop`, y para `TC_GC` y `TC_Net` donde se llaman.
4. `hw_draw2d.cpp`: incrementar contador `d2` en el bucle de comandos.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | Jugar MAP01-MAP02 3 min; log con `[FPS]` con `it, gc, net, wait` y `d2` (comandos 2D) |

**Marcadores que se quedan:** los de la pregunta abierta. **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 56`.

**Autocomprobación:**
1. **Tabla de diferencias.** Una sola pasada A. Se añaden 4 categorías de timing y se cambia el significado de `d2` (comandos en vez de imágenes); no cambia comportamiento, solo diagnóstico.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí: A muestra componentes nuevos altos o `d2` alto; B muestra componentes nuevos bajos.
3. **¿Lo que dices que se verá en pantalla puede verse?** El criterio es el log: `[FPS]` con `it, gc, net, wait, d2`.
4. **¿Se resuelve en una sola compilación?** Sí: una pasada A.
5. **¿Cada afirmación sobre una API tiene su fuente?** `D_DoomLoop` en d_main.cpp:1655, `GC::CheckGC` en d_main.cpp:1263, `NetUpdate` en d_net.cpp:2225, `Draw2D` en c3d_framebuffer.cpp:280, y `I_WaitForTic`/`I_WaitVBL` en d_net.cpp.
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Si `it` es casi igual a `avg` y los nuevos campos son bajos, el tiempo se va en lo que ya medimos (`sc`, `hd`, `gu`).
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí: una sola pasada, sin archivos de control.
8. **¿Lo que vas a añadir ya existe?** `TimingBegin/End`, `TimingCategory`, `TimingWindow/Worst` son variables existentes en `c3d_diag.cpp`. 4 nuevos valores del enum. `TimingTicsWindow` es variable existente. `d2` es una variable estática nueva en `hw_draw2d.cpp`. No se crea función ni archivo nuevo.
9. **¿Qué se ejecuta después de tu cambio?** Cada frame: `I_StartFrame` → `D_DoomLoop` (medido como `TC_Loop`) → `I_StartTic` + `D_ProcessEvents` (TC_Input) → `D_Display` pre-scene (TC_PreDisplay) → `TC_Scene3D` → `TC_HUD2D` → `TC_HDHud` → `TC_HDGPU` → `S_UpdateMusic` (TC_Sound) → `C3D_FrameEnd` (TC_FrameEnd). `[FPS]` acumula y muestra los nuevos campos con `it, gc, net, wait` y `d2`. El log sigue abierto (N3DS_Log en i_main_3ds.cpp:96).
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí: una sola pasada, sin archivos de control.
8. **¿Lo que vas a añadir ya existe?** `TimingBegin/End`, `TimingCategory`, `TimingWindow/Worst` son variables existentes en `c3d_diag.cpp`. 4 nuevos valores del enum. `TimingTicsWindow` es variable existente. `d2` es una variable estática nueva en `c3d_framebuffer.cpp`. No se crea función ni archivo nuevo.
9. **¿Qué se ejecuta después de tu cambio?** Cada frame: `I_StartFrame` → `TryRunTics` (medido como `TC_Loop`) → `I_StartTic` + `D_ProcessEvents` (TC_Input) → `D_Display` pre-scene (TC_PreDisplay) → `TC_Scene3D` → `TC_HDHud` → `TC_HDGPU` → `S_UpdateMusic` (TC_Sound) → `C3D_FrameEnd` (TC_FrameEnd). `[FPS]` acumula y muestra los nuevos campos con `it, gc, net, wait` y `d2`. El log sigue abierto (N3DS_Log en i_main_3ds.cpp:96).



## Prueba 57

**Lo que se pidió:**
- [ ] En `c3d_renderstate.cpp`: si existe `sdmc:/3ds/gzdoom/c3d-noflush.txt`, no llamar a `GSPGPU_FlushDataCache` en las líneas 609, 637 y 639.
- [ ] En `c3d_diag.cpp`: leer el interruptor `c3d-noflush.txt` (como los otros `[C3D]` switches) y añadirlo al log `[FPS]`.

**Pregunta:** ¿Evitar las llamadas redundantes a `GSPGPU_FlushDataCache` mejora el rendimiento o causa basura gráfica?

**Lo que ya sabemos (con la prueba que lo mostró):**
- Cada dibujo llama a `GSPGPU_FlushDataCache` (c3d_renderstate.cpp:609, 637 y 639), que es una llamada lenta al servicio GSP.
- Son redundantes: `C3D_FrameEnd(0)` (c3d_framebuffer.cpp:486) ya vacía toda la memoria lineal al final de cada imagen (citro3d renderqueue.c, C3D_FrameEnd; hecho F5).

**Hipótesis A:** Al quitar las llamadas redundantes, el tiempo de dibujo baja y no hay basura gráfica.
- La pasada B (con el archivo) mostrará: `[C3D] NoDraw=0 NoTex=0 Triangle=0 MinimalShader=0 TestFlip=0 MenuDiag=0 NoFlush=1` y no habrá problemas visuales. Los tiempos `sc` y `hdG` bajarán varios ms y los `avg` FPS subirán respecto a la pasada A.

**Hipótesis B:** Al quitar las llamadas redundantes, aparece basura gráfica (triángulos rotos, parpadeos) porque algo necesita ese flush, o los tiempos de dibujo no cambian significativamente.
- La pasada B (con el archivo) mostrará: `[C3D] NoDraw=0 NoTex=0 Triangle=0 MinimalShader=0 TestFlip=0 MenuDiag=0 NoFlush=1` y Alex reportará basura gráfica o tiempos similares a la pasada A.

**Única variable que cambia:** `c3d_renderstate.cpp` y `c3d_diag.cpp`, bajo `#ifdef __3DS__`:
1. `c3d_renderstate.cpp`: si existe `sdmc:/3ds/gzdoom/c3d-noflush.txt`, no llamar a esas tres `GSPGPU_FlushDataCache` (se consulta el bool `NoFlush` previamente cargado).
2. `c3d_diag.h`: añadir `extern bool NoFlush;`.
3. `c3d_diag.cpp`: leer el interruptor y añadir `noflush=%d` a la línea de switches (l.91) y a la de `[FPS]` (l.155). El buffer `buf[512]` es suficiente para los ~410 chars previstos.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | Jugar MAP01-MAP02 3 min; log con `[FPS]` (comportamiento actual) |
| B | `c3d-noflush.txt` | Jugar MAP01-MAP02 3 min; ¿hay basura gráfica? |

**Marcadores que se quedan:** la pregunta abierta. **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 57`.

**Autocomprobación:**
1. **Tabla de diferencias.** Una sola variable: el uso de `GSPGPU_FlushDataCache`. Pasada A (sin archivo) = actual. Pasada B (con archivo) = sin las llamadas.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí: A (sin basura, mejora rendimiento); B (con basura).
3. **¿Lo que dices que se verá en pantalla puede verse?** El criterio es el log: `[FPS]` con el flag del interruptor. La pantalla muestra gameplay normal o con basura.
4. **¿Se resuelve en una sola compilación?** Sí: dos pasadas A y B con el mismo `.3dsx`.
5. **¿Cada afirmación sobre una API tiene su fuente?** `GSPGPU_FlushDataCache` (c3d_renderstate.cpp:609, 637, 639); `C3D_FrameEnd` (c3d_framebuffer.cpp:486); hecho F5.
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Si en B hay basura → B. Si en B no hay basura y el tiempo baja → A.
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí: solo poner o quitar el archivo en la SD.
8. **¿Lo que vas a añadir ya existe?** El patrón de lectura de interruptores en `c3d_diag.cpp` y el uso de `GSPGPU_FlushDataCache` en `c3d_renderstate.cpp` ya existen. No se crea nada nuevo, solo se condiciona.
9. **¿Qué se ejecuta después de tu cambio?** Cada frame: `C3D_FrameBegin` → `C3D_FrameEnd` (con o sin flush según el archivo). El log sigue abierto (N3DS_Log en i_main_3ds.cpp:96).

## Prueba 58

**Lo que se pidió:**
- [x] En `c3d_renderstate.cpp`: quitar las tres llamadas a `GSPGPU_FlushDataCache` (cerca de las líneas 610, 640 y 644).
- [x] Quitar el interruptor `c3d-noflush` (ya que `C3D_FrameEnd(0)` ya vacía la caché, hecho F5).

**Pregunta:** ¿Al quitar las llamadas redundantes y el interruptor, el rendimiento se mantiene y no hay basura gráfica?

**Lo que ya sabemos (con la prueba que lo mostró):**
- Prueba 57: sin las tres `GSPGPU_FlushDataCache`, el rendimiento sube (~45 a ~57 fps) y `hdG` baja (5 a 1,5 ms), sin basura gráfica.
- Hecho F5: `C3D_FrameEnd(0)` ya vacía la memoria lineal al final de cada imagen.

**Hipótesis A:** El cambio es seguro. El rendimiento se mantiene igual que en la pasada B de la 57 y no hay basura gráfica.
- Si A es cierta, el log mostrará: sin el flag `NoFlush` y con tiempos de dibujo bajos.

**Hipótesis B:** Al quitar las llamadas, aparece basura gráfica.
- Si B es cierta, Alex reportará basura gráfica.

**Única variable que cambia:**
`c3d_renderstate.cpp` y `c3d_diag.cpp`, bajo `#ifdef __3DS__`: eliminar las llamadas a `GSPGPU_FlushDataCache` y el código de lectura del interruptor `c3d-noflush.txt`.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | Jugar MAP01-MAP02 3 min; rendimiento normal y sin basura gráfica |

**Marcadores que se quedan:** la pregunta abierta. **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 58`

**Autocomprobación:**
1. **Tabla de diferencias.** Una sola variable: el uso de `GSPGPU_FlushDataCache` (se quita). No hay pasadas comparativas de shader.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí. A (sin basura, rendimiento OK); B (con basura).
3. **¿Lo que dices que se verá en pantalla puede verse?** Sí, Alex verá si hay basura gráfica o no.
4. **¿Se resuelve en una sola compilación?** Sí.
5. **¿Cada afirmación sobre una API tiene su fuente?** Sí: `GSPGPU_FlushDataCache` (c3d_renderstate.cpp:609, 637, 639); `C3D_FrameEnd` (c3d_framebuffer.cpp:486); hecho F5.
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Si no hay basura pero el rendimiento no mejora, que la redundancia no era el problema principal (pero ya sabemos que sí lo era por la 57).
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí.
8. **¿Lo que vas a añadir ya existe?** Se elimina código existente.
9. **¿Qué se ejecuta después de tu cambio?** `C3D_FrameBegin` → `C3D_FrameEnd`. El log sigue abierto.

## Prueba 59

**Lo que se pidió:**
- [x] Implementar el desplazamiento de profundidad (`SetDepthBias`) solicitado por el motor.
- [x] `SetDepthBias(-1,-128)` en `src/rendering/hwrenderer/scene/hw_walls.cpp:129` y `hw_sprites.cpp:165`.
- [x] `SetDepthBias(1,128)` en `hw_flats.cpp:254`.
- [x] En `C3DRenderState::Apply`, cuando cambie `mBias`, llamar a `C3D_DepthMap(true, -1.0f, mBias.mUnits * c3d_depthbias)`.
- [x] `c3d_depthbias` es un nuevo CVAR float, por defecto `1.0/65536`.
- [x] Loguear una vez el valor usado de `c3d_depthbias`.

**Pregunta:** ¿El uso de `SetDepthBias` mediante `C3D_DepthMap` elimina el parpadeo de las manchas de sangre y balas?

**Lo que ya sabemos (con la prueba que lo mostró):**
- El estado de profundidad está en `mBias` (`src/common/rendering/hwrenderer/data/hw_renderstate.h:258, 610-624`).
- El motor pide desplazamientos de profundidad para sangre/balas y planos, pero el backend c3d no los aplica.
- citro3d `effect.c:10` usa `C3D_DepthMap(true, -1.0f, value)`.

**Hipótesis A:** El arreglo funciona. Las manchas de sangre y balas dejan de parpadear.
- Si A es cierta, el log mostrará: `[C3D] depthbias=0.000015` (o similar) y Alex no verá parpadeo.

**Hipótesis B:** El arreglo no funciona o causa otros problemas (como falta de profundidad en otros elementos).
- Si B es cierta, el parpadeo persiste o la imagen se rompe.

**Única variable que cambia:**
`src/common/rendering/hwrenderer/data/hw_renderstate.h`, `src/common/rendering/c3d/c3d_renderstate.cpp`, `src/rendering/hwrenderer/scene/hw_walls.cpp`, `src/rendering/hwrenderer/scene/hw_sprites.cpp`, `src/rendering/hwrenderer/scene/hw_flats.cpp`, y la definición del nuevo CVAR `c3d_depthbias`.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | `c3d_depthbias=0.000015` (o similar) en el ini | Jugar MAP01-MAP02 3 min; ¿parpadean las manchas de sangre/balas? |

**Marcadores que se quedan:** la pregunta abierta. **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 59`

**Autocomprobación:**
1. **Tabla de diferencias.** Una sola variable: el uso de `C3D_DepthMap` condicionado a `mBias` y el nuevo CVAR `c3d_depthbias`.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí. A (sin parpadeo, log con bias); B (con parpadeo o imagen rota).
3. **¿Lo que dices que se verá en pantalla puede verse?** Sí, el parpadeo es visible.
4. **¿Se resuelve en una sola compilación?** Sí.
5. **¿Cada afirmación sobre una API tiene su fuente?** Sí: `SetDepthBias` (hw_walls.cpp:129, hw_sprites.cpp:165, hw_flats.cpp:254); `mBias` (hw_renderstate.h:258, 610-624); `C3D_DepthMap` (citro3d effect.c:10).
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Si el parpadeo persiste, que el error no es el desplazamiento de profundidad o que el valor/llamada no es el correcto.
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí.
8. **¿Lo que vas a añadir ya existe?** Se añade un nuevo CVAR y se usa una función de citro3d existente.
9. **¿Qué se ejecuta después de tu cambio?** `C3DRenderState::Apply` → `C3D_DepthMap`. El log sigue abierto.

**Autocomprobación:**
1. **Tabla de diferencias.** Una sola variable: el uso de `GSPGPU_FlushDataCache` (se quita). No hay pasadas comparativas de shader.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí. A (sin basura, rendimiento OK); B (con basura).
3. **¿Lo que dices que se verá en pantalla puede verse?** Sí, Alex verá si hay basura gráfica o no.
4. **¿Se resuelve en una sola compilación?** Sí.
5. **¿Cada afirmación sobre una API tiene su fuente?** Sí: `GSPGPU_FlushDataCache` (c3d_renderstate.cpp:609, 637, 639); `C3D_FrameEnd` (c3d_framebuffer.cpp:486); hecho F5.
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Si no hay basura pero el rendimiento no mejora, que la redundancia no era el problema principal (pero ya sabemos que sí lo era por la 57).
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí.
8. **¿Lo que vas a añadir ya existe?** Se elimina código existente.
9. **¿Qué se ejecuta después de tu cambio?** `C3D_FrameBegin` → `C3D_FrameEnd`. El log sigue abierto.


## Prueba 60

**Lo que se pidió:**
- [x] Eliminar `GSPGPU_FlushDataCache` (líneas 650 y 654 de `src/common/rendering/c3d/c3d_renderstate.cpp`) y todo el mecanismo `NoFlush` (`c3d_diag.h`/`.cpp` y switches).
- [x] Cambiar el valor por defecto del CVAR `c3d_depthbias` a `-0.00005`.

**Pregunta:** ¿Con los cambios aplicados se eliminan las llamadas redundantes de flush y mejora la estabilidad visual con un depth bias corregido?

**Lo que ya sabemos (con la prueba que lo mostró):**
- Los flushes pueden impactar el rendimiento o no ser necesarios.
- CVARs en 3DS se definen bajo `#ifdef __3DS__`.

**Hipótesis A:** Los cambios funcionan y eliminan las llamadas innecesarias sin efectos secundarios, mejorando la visualización de sangre/agujeros.
- Si A es cierta, el log no contendrá rastros de `NoFlush` ni `GSPGPU_FlushDataCache`, y se verá lo esperado.

**Hipótesis B:** La eliminación del flush o el cambio de bias causa efectos secundarios visuales o crashes.
- Si B es cierta, el log mostrará errores o se verá inconsistencia visual (parpadeo).

**Única variable que cambia:** 
1. `src/common/rendering/c3d/c3d_renderstate.cpp`: borradas líneas de flush y condicional `NoFlush`.
2. `src/common/rendering/c3d/c3d_diag.h` y `.cpp`: eliminación del interruptor `NoFlush`.
3. CVAR `c3d_depthbias` default en 3DS.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | Sangre y agujeros de bala visibles, sin parpadeo. |

**Marcadores que se quedan:** los de la pregunta abierta. **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 60`.

**Autocomprobación:**
1. **Tabla de diferencias.** No hay pasadas comparativas, solo un cambio de estado y de default.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí: A confirma la limpieza; B muestra fallos o parpadeo.
3. **¿Lo que dices que se verá en pantalla puede verse?** Sí (sangre/agujeros).
4. **¿Se resuelve en una sola compilación?** Sí.
5. **¿Cada afirmación sobre una API tiene su fuente?** Sí.
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Que la limpieza es correcta pero no influye en lo visual.
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí.
8. **¿Lo que vas a añadir ya existe?** No se añade nada nuevo.
9. **¿Qué se ejecuta después de tu cambio?** El loop normal del juego con el novo bias y sin flushes.
