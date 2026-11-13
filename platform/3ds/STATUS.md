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
11. El automapa no dibuja líneas: el backend aún no dibuja líneas ni puntos.

## Pregunta abierta

**¿En qué paso de `D_Display()` se para el cierre desde HOME?** (prueba 37). Después, el menú (prueba 38).

## Prueba 37 (en preparación: solo el cierre desde HOME)

**Pregunta:** ¿qué paso de `D_Display()` → `C3DFrameBuffer::Update()` no vuelve al cerrar desde HOME?

**Hipótesis** (detalle de cada una: commit `a6257348d`):
- **S:** `ScreenBegin()` no vuelve: se queda en `C3D_FrameBegin` o en `gspWaitForVBlank`. Última marca `FB_before` o `VBlank_before`; no sale `FB_after` ni `SB_ok`.
- **A1:** `C3D_FrameBegin` falla los reintentos (`FB_fail final`) y el camino "bloqueado" de `Update()` se para: salen `blk_*` hasta donde llegue, no sale `upd_end`.
- **B:** `ScreenBegin()` termina (`SB_ok`) y el camino normal se para en `Draw2D()` o `C3D_FrameEnd()`: no sale `upd_end`.
- **C:** `Update()` termina (`upd_end`) y se para después, en `twod->OnFrameDone()` (d_main.cpp:1113): no sale `D_Display done`.
- (La variante "el camino bloqueado termina" ya la descarta la 36: habría salido `D_Display done`.)
- Ojo: si el fotograma ya empezó antes (`mFrameStarted`), `ScreenBegin()` vuelve sin hacer nada y sus marcas no salen.

**Marcas:** las de `ScreenBegin()` y `Update()` en `c3d_framebuffer.cpp` (commit `a6257348d`), y las `[EXIT]` de `d_main.cpp`, que se quedan. **Falta:** las nuevas tienen que escribir solo con `gameloop_abort` activo, como las de `d_main.cpp`; ahora escriben y hacen `fsync` en cada fotograma desde el arranque.

**Pasada única:** esperar al título (~100 s), HOME → volver, esperar 60 s, HOME → cerrar, esperar 15 s. Criterio: el log.

**Por decidir:** si una segunda pasada (archivo de control) no llama a `D_Display()` tras la orden de cerrar, como posible arreglo, va aquí o en la 38.

Azahar: pendiente RomFS

## Prueba 38 (siguiente: el menú)

Pendiente de plantilla, después de la 37. Lo que tiene que comparar, menú frente a HUD, en unos pocos fotogramas de cada uno y con el número de fotograma: cuántos `Draw2D` hay por fotograma, el viewport y el tamaño del `F2DDrawer`, y del primer comando que dibuja: sus vértices, el tamaño de la textura, el wrap y la matriz de transformación (con decimales). Las hipótesis tienen que explicar que el menú salga **repetido**, y que el HUD, la consola y el título se vean bien. Puede tener relación con el cierre en OPTIONS (punto 10): menús con mucho texto, muchos comandos de GPU.

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
| 37 | ¿Qué paso de `D_Display()` se para al cerrar desde HOME? | En preparación (ver abajo) |
| 38 | Menú grande y repetido | Siguiente, tras la 37 |


## Pendientes que no son la pregunta abierta

- **Azahar:** la prueba 36 se cierra a los 6 s con "Cannot find a game IWAD", aunque `doom2.wad` está en la SD del emulador. Hipótesis: no se monta la RomFS del `.3dsx` (`romfsInit()` no se comprueba, `i_main_3ds.cpp:199`).
- **OPTIONS:** el búfer de comandos de la GPU se llena (punto 10). Archivos en `~/work/3ds-ports/pruebas/37/crash-options-*`.
- Una línea del log tiene texto de otro idioma pegado antes de `Selecting OpenGL backend...`.
- Líneas y puntos (automapa), niebla, luz, portales, sonido, pantalla de abajo, `.cia` (unique ID 0xF3D5C).
- Las fuentes TrueType latinas generan atlas de 1 MB con todo el juego de caracteres: lento y caro en memoria. Recortarlo cuando el resto funcione.
