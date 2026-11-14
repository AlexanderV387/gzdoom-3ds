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
| 90 | ¿`romfsInit()` falla en Azahar? | ✅ RomFS funciona: `init=OK`, `pk3: YES`. El fallo "Cannot find a game IWAD" es por falta de `doom2.wad` en la ruta del motor, no por RomFS. |


## Pendiente

Sin pruebas activas.

## Pendientes que no son la pregunta abierta

- **Azahar:** la prueba 36 se cierra a los 6 s con "Cannot find a game IWAD", aunque `doom2.wad` está en la SD del emulador. RomFS funciona correctamente en Azahar (prueba 90: `romfsInit()` OK, `uzdoom.pk3` presente). El problema está en la búsqueda de IWAD: Azahar no tiene `doom2.wad` en la ruta que el motor consulta.
- **OPTIONS:** el búfer de comandos de la GPU se llena (punto 10). Archivos en `~/work/3ds-ports/pruebas/37/crash-options-*`.
- **Cierre lento:** medir con los T+ms del log cuánto tarda desde `gameloop_abort set` hasta la última marca (prueba 42: T+102341 a T+130166 = ~28 s, pero incluye tiempo hasta el suspend).
- **Música del HOME:** se corta al cerrar el juego y tarda en volver (pruebas 41-42).
- Una línea del log tiene texto de otro idioma pegado antes de `Selecting OpenGL backend...`.
- Líneas y puntos (automapa), niebla, luz, portales, sonido, pantalla de abajo, `.cia` (unique ID 0xF3D5C).
- Las fuentes TrueType latinas generan atlas de 1 MB con todo el juego de caracteres: lento y caro en memoria. Recortarlo cuando el resto funcione.
