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

## Prueba 43 (diseño de Claude sobre el borrador de Catequilla, `934a7aa3b`)

**Lo que se pidió:** medir, en la consola y en Azahar con la misma compilación, qué hace distinto el menú que el HUD al dibujar en 2D. [x]

**Pregunta:** ¿El menú manda a la GPU algo que el HUD no manda (geometría muy fuera de la pantalla, una textura especial o un scissor fuera de rango), y es igual en la consola que en Azahar?

**Lo que ya sabemos:** punto 9 (menú grande y repetido en la consola; HUD, título y consola `~` bien); Z7 (menú perfecto en Azahar, también con el renderizador Software); punto 34 (escala del motor normal). Lo que el emulador hace distinto de la PICA200 real suele estar en: coordenadas de vértices muy grandes (la consola no las recorta igual), texturas raras (tamaño, lienzos/canvas) y scissor fuera del framebuffer.

**Interruptor:** `c3d-menu-diag.txt` en `sdmc:/3ds/gzdoom/` → `Diag::MenuDiag` (`c3d_diag.cpp`, junto a `TestFlip`). Sin el archivo no se escribe nada.

**Qué se registra** (solo con el interruptor; 3 fotogramas con el menú abierto, `menuactive != MENU_Off`, y 3 con el HUD, `gamestate == GS_LEVEL` y sin menú; cada línea con `f<N>` = `Diag::FrameN()`):
- `[MENU] f<N> d2d vp=x,y,w,h drawer=WxH cmds=C verts=V` al entrar en `Draw2D` (`hw_draw2d.cpp`, tras `Set2D`).
- `[MENU] f<N> geo min=x,y max=x,y far=K`: mínimo y máximo de los vértices de todos los comandos, aplicando `cmd.transform` cuando `cmd.useTransform`; `K` = comandos con algún vértice fuera de [-400, 800] × [-240, 480].
- Por cada **textura distinta** del fotograma (como mucho 12): `[MENU] f<N> tex <nombre> orig=WxH canvas=0/1 wrap=0/1 xform=0/1 shape=0/1 indexed=0/1` (`cmd.mTexture->GetName()`, `GetTexelWidth/Height`, `isHardwareCanvas()`, `DTF_Wrap`, `useTransform`, `shape2DBufInfo != nullptr`, `DTF_Indexed`).
- `[MENU] f<N> sci n=S min=l,t max=r,b`: cuántos comandos usan scissor y el rango de los valores **ya girados** que recibe `C3D_SetScissor` (`c3d_renderstate.cpp`, `SetScissor`).
- En la textura subida: `[MENU] up <ptr> WxH -> PWxPH canvas=0/1` en `BindOrCreate` (`c3d_hwtexture.cpp`, cuando `!mCreated`), con `w`, `h` y `TexDimension(w/h)`; `<ptr>` es la dirección de `tex` (`%p`), y en la línea `tex` de `Draw2D` añade también `cmd.mTexture->GetTexture()` con `%p` para poder emparejarlas. Solo la primera vez que se crea cada una mientras el interruptor está activo.

Todo con `snprintf` + `N3DS_Log` (S10). Unas 20-60 líneas en total, no en cada fotograma.

**Hipótesis** (cada una explica "solo el menú", "solo la consola" y "grande y repetido"):
- **G (geometría):** algún comando del menú tiene vértices muy lejos de la pantalla; la PICA200 real los rasteriza mal (se desbordan y se repiten) y el emulador los recorta. Log: `far > 0` en los fotogramas del menú y `far = 0` en los del HUD.
- **T (textura):** el menú usa una textura que el HUD no usa y que la consola lee distinto (un canvas, una textura con wrap o subida con otro tamaño). Log: una línea `tex`/`up` que solo sale con el menú y con `canvas=1`, `wrap=1` o un tamaño raro.
- **S (scissor):** algún comando del menú usa un scissor fuera de 240 × 400 o invertido. Log: `sci` con valores negativos, `r > 240`, `b > 400` o `l > r`.
- **Ninguna:** valores normales e iguales en la consola y en Azahar. Entonces el fallo está en cómo la GPU real interpreta un estado que aún no medimos, y la 44 lo acota cambiando una cosa cada vez.

**Pasadas:**
| Dónde | Archivos de control | Qué hacer |
|---|---|---|
| Consola (Alex) | `c3d-menu-diag.txt` | Título, abrir el menú, esperar 5 s, cerrar el menú, empezar MAP01 y jugar 10 s. **Foto del menú.** Pasar `log.txt` |
| Azahar (Catequilla) | `c3d-menu-diag.txt` en la SD de Azahar | `prueba-azahar.sh 43 300 abierto`, tecla `m` (Start), 5 s, `azahar-captura.sh menu43`, Start otra vez, `a` para empezar, 10 s, cerrar |

**Criterio:** comparar los dos logs línea a línea y la foto con la captura.

**Marcadores que se quedan:** todos los existentes. **Cómo se compila:** `platform/3ds/docker-build-test.sh 43`.

**Autocomprobación:**
1. Tabla de diferencias: entre consola y Azahar solo cambia la plataforma (misma compilación, mismo interruptor); no se cambia ningún dibujo.
2. Cada hipótesis predice un log distinto: G → `far > 0` solo en el menú; T → una `tex`/`up` exclusiva del menú con valores raros; S → `sci` fuera de rango; ninguna → todo normal e igual.
3. La pantalla no cambia con el interruptor; el criterio es el log, más la foto y la captura para comparar la forma del fallo.
4. Una sola compilación sirve para la consola y Azahar; si sale "ninguna", la 44 cambia un estado cada vez.
5. Fuentes: `hw_draw2d.cpp` (`Draw2D`: `SetViewport`, `Set2D`, bucle de comandos, `useTransform`, `SetMaterial`), `c3d_renderstate.cpp` (`SetScissor`, `SetViewport`), `c3d_hwtexture.cpp` (`CreateTexture`, `BindOrCreate`, `TexDimension`), `v_2ddrawer.h` (`TwoDVertex` con `x, y, z`; `RenderCommand`).
6. Si todo sale igual, sé que los valores que mandamos son normales y que la diferencia está en un estado que no se mide aquí.
7. Un solo archivo de control vacío, el mismo en los dos sitios.
8. `MenuDiag` no existe (`grep -rn MenuDiag src/`); sigue el patrón de `TestFlip` en `c3d_diag`.
9. Las marcas solo leen datos y escriben en el log, que sigue abierto; no esperan a la GPU ni cambian el dibujo.

## Pendientes que no son la pregunta abierta
- **OPTIONS:** el búfer de comandos de la GPU se llena (punto 10). Archivos en `~/work/3ds-ports/pruebas/37/crash-options-*`.
- **Cierre lento:** medir con los T+ms del log cuánto tarda desde `gameloop_abort set` hasta la última marca (prueba 42: T+102341 a T+130166 = ~28 s, pero incluye tiempo hasta el suspend).
- **Música del HOME:** se corta al cerrar el juego y tarda en volver (pruebas 41-42).
- Una línea del log tiene texto de otro idioma pegado antes de `Selecting OpenGL backend...`.
- Líneas y puntos (automapa), niebla, luz, portales, sonido, pantalla de abajo, `.cia` (unique ID 0xF3D5C).
- Las fuentes TrueType latinas generan atlas de 1 MB con todo el juego de caracteres: lento y caro en memoria. Recortarlo cuando el resto funcione.
