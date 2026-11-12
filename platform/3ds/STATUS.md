# UZDoom 3DS: estado (leer al empezar cada conversación)

Es el único estado válido del port. Se actualiza al cerrar cada prueba: la tabla, "Lo que sabemos" y "Pregunta abierta". Lo escribe quien trabaja (Catequilla o Claude), en español, corto.

**Rama de trabajo:** `uzdoom-3ds-test25` (parte de `catequilla-test24`, que guarda las pruebas 17 a 24 tal como quedaron).
**Compilar:** `platform/3ds/docker-build-test.sh NN` en Ubuntu, con devkitPro dentro de Docker (su instalador responde 403); con devkitPro instalado, `build-test.sh NN`. Ver README.
**Pruebas guardadas:** `~/work/3ds-ports/pruebas/NN/` (`.3dsx`, `.elf`, `info.txt`).

## Lo que sabemos (con la prueba que lo mostró)

1. El motor arranca entero: IWAD, ZScript, menús, playsim y backend (`Init complete`, pruebas 7 y 13).
2. La prueba 8 se cerraba en `I_Rumble`: el gestor del joystick está vacío en la 3DS (pruebas 11 y 12, arreglado). **Ojo:** eso también significa que quizá la entrada de mandos nunca se inicia. Revisarlo cuando haya imagen.
3. La imagen de la pantalla de carga a 1280x960 no cabía en la memoria: ahora es de 640x480 (prueba 13).
4. **El primer fotograma con un dibujo cuelga la GPU.** El fotograma se envía (`C3D_FrameEnd`), pero la GPU nunca termina: el siguiente `C3D_FrameBegin` falla durante 2 s y todos los siguientes también (pruebas 16 a 23).
5. Ese primer fotograma tiene **un solo dibujo**: un `DrawElements` 2D de 12 índices, stride 24, tres atributos (posición, coordenadas de textura, color) y con textura (pruebas 17 y 21).
6. Saltar el trabajo del fotograma 0 solo **mueve** ese dibujo al fotograma 1, que se cuelga igual (prueba 22). Eso **no** demuestra que el contenido no importe: el dibujo seguía ahí.
7. Los logs pueden perder el final si se apaga la consola: antes solo se hacía `fflush`, que no escribe en la SD (`hechos-3ds.md`, S2). Desde la prueba 25 hay `fsync`.
8. **El comando de dibujo es la causa del cuelgue:** sin dibujo enviado (`c3d-nodraw.txt`), los fotogramas 0-11 corren sin problemas (prueba 25, pasada B, línea 51-73).
9. **La textura no es la causa:** sin textura (`c3d-notex.txt`), el dibujo se envía igual y la GPU se cuelga (prueba 25, pasada D, línea 50-53).
10. **El problema no son los buffers del motor:** un triángulo mínimo con `DrawArrays`, vértices en memoria lineal y sin textura también cuelga la GPU (prueba 25, pasada C, línea 49-51). El shader del motor se usa en ambos casos.
11. **El cuelgue depende del shader o de sus datos.** Con la configuración de `simple_tri` (shader distinto, matriz `Mtx_OrthoTilt`, vértices en coordenadas de pantalla con z=0.5, dos atributos fijos) el triángulo se dibuja correctamente (prueba 26, log A). Con el shader del motor y sus datos (matriz identidad, vértices en clip space con z=-0.5, tres atributos) la GPU se cuelga (prueba 26, log B). La pasada cambió shader, matriz, vértices y atributos a la vez: no se sabe cuál es la causa exacta. Los uniformes del shader del motor se cargaron correctamente (log A, línea 46: `DVLE=1 mvp=0 colorscale=4 colormul=5 texmat=6`).
12. **El output de texcoord (`outtc0`) es la causa del cuelgue.** En la prueba 27, las variantes sin `outtc0` (V1, V3) funcionan y las que lo tienen (V0, V2) se cuelgan. Los datos del motor no son el problema (V4: shader simple_tri con datos del motor funciona). No son los 8 uniformes (V3 funciona). No es el `mul` del color (V2 lo quita pero se cuelga igual porque tiene `outtc0`).
13. **Las instrucciones de texcoord que calculan el valor con `dp4` son la causa específica.** No es la declaración de `outtc0` (prueba 28 V0 con constante funciona), ni la falta de textura (V2 con textura se cuelga igual). Entre V0 y V1 hay tres diferencias: instrucción (`mov` vs `dp4`), fuente (constante vs uniform+registro temporal) y componentes escritos (`.xyzw` vs solo `.xy`) (prueba 28).
14. **Faltar escribir `outtc0.zw` es la causa del cuelgue.** Cuando `dp4` escribe solo `.x` y `.y` de `outtc0`, los componentes `.z` y `.w` quedan indefinidos y la GPU se cuelga. Añadir `mov outtc0.zw, ones` lo arregla (prueba 29, V1 repetida y V2 OK vs V0/V3 HANG). No importa si el source de `dp4` es `intex` directo o un registro temporal `r2`. Todas las pasadas fueron con el juego real (sin `c3d-triangle.txt`, `triangle=0` en todos los logs).
15. **La carga del juego real tarda ~2 minutos** antes de mostrar la pantalla de título. La causa se desconoce (prueba 29, V1 repetida y V2).
16. **El arreglo permanente (`mov outtc0.zw, ones`) funciona con el juego real.** La prueba 30 arrancó sin archivos de control, mostró la pantalla de título y la de créditos alternándose, sin GPU HANG. La imagen está volteada verticalmente (arriba↔abajo).
17. **Texman.Init es el cuello de botella de carga.** Con los timestamps corregidos de la prueba 31, `Texman.Init: Init texture manager.` tarda ~51 s (53% del total de ~97 s). Le sigue un hueco de binding de teclas (~13.6 s, 14%) y `M_LoadDefaults` (~10 s, 10%).
18. **El volteo vertical no está en la geometría.** La prueba 31 V0 (`testflip=1`) dibuja un cuadrado rojo en coordenadas (0,0) y uno verde en (0,180). En pantalla, el rojo aparece arriba y el verde abajo: Y crece hacia abajo, igual que en la 3DS. El pipeline geométrico conserva Y correctamente. El volteo debe estar en las texturas (`CreateTexture`, `c3d_hwtexture.cpp`).
19. **El volteo vertical está en el orden de filas de `CreateTexture`.** La fila 0 de memoria del PICA200 es la parte SUPERIOR de la textura, no la inferior. Cambiar `sy = y * h / ph` a `sy = h - 1 - y * h / ph` (copiar de abajo arriba) corrige la imagen por completo: logo arriba, créditos en orden, letras derechas (prueba 32).
20. **El motor no llama a `aptMainLoop()` ni a `hidScanInput()`.** El main loop es `GameMain()` → `D_DoomMain_Internal()` con `for(;;)` (d_main.cpp línea 1598). Sin `aptMainLoop()`, `aptHandleJumpToHome()` nunca se ejecuta: el botón HOME no funciona. El input va por SDL, pero `hidScanInput()` no se llama. Los ejemplos de 3ds-examples (`templates/application/source/main.c` líneas 6-28) y todos los ports usan `while(aptMainLoop()) { hidScanInput(); ... }` como patrón estándar (libctru `apt.c` línea 851-856, `hid.c` línea 172-219).
21. **El cierre desde HOME se para entre `TryRunTics done` y `D_Display done`.** La prueba 35 muestra que el bucle llega a la iteración con `gameloop_abort=true` y `TryRunTics()` completa, pero no llega a `[EXIT] D_Display done`. Las funciones entre ambas marcas son `I_StartTic()` (d_main.cpp:1652), `D_ProcessEvents()` (l.1653) y `D_Display()` (l.1654). Es una de las tres donde se para.
22. **`D_Display()` es la función que se cuelga.** La prueba 36 muestra `Before D_Display` en el log pero no `D_Display done`: `I_StartTic()` y `D_ProcessEvents()` completan sin problemas. El cuelgue es dentro de `D_Display()`, probablemente por el contexto GPU tras el APT suspend/restore.

## Prueba 28 (hecha, 3 pasadas, logs analizados)

**Pregunta:** ¿El cuelgue es por tener `outtc0` declarado como output del shader, o por escribirle valores calculados con `dp4`?

**Resultado:**

| Var | Shader | `outtc0` | Textura | v28= | Log | Resultado |
|-----|--------|----------|---------|------|-----|-----------|
| V0 | consttc (mov constante) | sí (0,0,0,0) | no | 0 | 1749 líneas, frames 0-11 ok | ✅ OK |
| V1 | c3d_main (full, dp4) | sí (dp4) | no | 1 | 68 líneas, HANG frame 1 | ❌ HANG |
| V2 | c3d_main (full, dp4) | sí (dp4) | **sí** (1x1) | 2 | 69 líneas, HANG frame 1 | ❌ HANG |

**Veredicto de hipótesis:**

| Hipótesis | Resultado | Razón |
|---|---|---|
| A: `outtc0` en sí es el problema | ❌ Descartada | V0 tiene `outtc0` declarado y funciona. |
| B: `dp4` escribiendo `outtc0` es el problema | ✅✅ **Confirmada** | V0 (sin dp4, constante) OK. V1 (con dp4) HANG. V2 (con dp4 + textura) HANG. |
| C: `outtc0` + tex env sin textura | ❌ Descartada | V2 tiene textura bindada y se cuelga igual. |

**Conclusión:** Entre V0 (OK) y V1 (HANG) hay **tres** diferencias en las instrucciones de texcoord:
1. Instrucción: `mov` (V0) frente a `dp4` (V1).
2. Fuente: constante `zeros` (V0) frente a uniform `texmat` + registro temporal `r2` (V1).
3. Componentes escritos: `outtc0` completo `.xyzw` (V0) frente a solo `.x` y `.y` por instrucción (V1).
La causa es una o más de estas tres. No es la declaración de `outtc0` ni la falta de textura.

**Orientación del triángulo:** V0: triángulo apuntando a la izquierda (matriz identidad, clip space). V1/V2: pantalla negra (HANG).

**Lo que ya sabemos:**
- Test 27: `outtc0` es la causa del cuelgue.
- Test 28 V0: `outtc0` declarado pero con valor constante → funciona. El problema no es la declaración.
- Test 28 V1: `dp4` con `texmat` → HANG.
- Test 28 V2: `dp4` con `texmat` + textura dummy → HANG. El problema no es la falta de textura.
- Los uniformes se setean en `SetEngineUniforms()` (c3d_framebuffer.cpp líneas 233-245): `texmat` es identity rows `(1,0,0,0)` y `(0,1,0,0)`.
- El input attribute `intex` (v1) está fijo a `(0,0,0,0)` (c3d_framebuffer.cpp línea 327).

## Prueba 37 (pendiente)

**Lo que se pidió:**
- [x] Diagnosticar qué paso dentro de `D_Display()` → `C3DFrameBuffer::Update()` se cuelga al cerrar desde HOME.
- [x] Medir los valores del frame con menú vs frame con solo HUD, para el diagnóstico del zoom del menú.
- [x] Dato nuevo: el HUD se ve bien (Alex, en consola). Solo el menú sale zoomado y repetido arriba y abajo.

**Pregunta:** ¿Qué paso dentro de `C3DFrameBuffer::Update()` se cuelga al cerrar desde HOME?

**Lo que ya sabemos (con la prueba que lo mostró):**
- `D_Display()` es la función que se cuelga (prueba 36, log l.91).
- `D_Display()` llama a `End2DAndUpdate()` (d_main.cpp:1571), que hace `twod->End()` → `screen->Update()` → `twod->OnFrameDone()` (d_main.cpp:1108-1113).
- `C3DFrameBuffer::Update()` (c3d_framebuffer.cpp:323-349): `ScreenBegin()` (l.325) → si no está blocked, `Draw2D()` (l.340) → `twod->Clear()` (l.342) → `C3D_FrameEnd(0)` (l.343) → `mFrameStarted=false` (l.344) → `Diag::FrameDone(true)` (l.345).
- `ScreenBegin()` (c3d_framebuffer.cpp:142-168): `C3D_FrameBegin(SYNCDRAW|NONBLOCK)` con hasta 120 reintentos (1 si `Diag::HangReported()`). Si falla, pone `mFrameBlocked=true` y retorna.
- Si `mFrameBlocked`, `Update()` salta el dibujo: `twod->Clear()` → `ResetDeviceState()` → `BeginFrame()` → `Diag::FrameDone(false)` → `Super::Update()` → `mFrameBlocked=false` → return (l.326-336).
- Los frames 0-11 funcionan sin problemas: el pipeline GPU está sano al inicio.
- Tras el APT suspend/restore, `aptMainLoop()` devuelve false (prueba 34, l.106). El contexto GPU se recupera (hechos-3ds.md S9: `GSPGPU_AcquireRight` + restaurar VRAM).
- El HUD se ve bien (dato de Alex, en consola). Solo el menú sale zoomado y repetido arriba y abajo.
- Preguntas de contraste sobre el zoom del menú (pendiente de confirmar con Alex): ¿la consola (~) se ve bien?, ¿el automapa?, ¿la pantalla de título?

**Hipótesis A:** `ScreenBegin()` / `C3D_FrameBegin()` no avanza. El contexto GPU está corrupto tras el APT suspend: la GPU no responde y `C3D_FrameBegin` falla los 120 reintentos (2 s), pone `mFrameBlocked=true`, y el bucle repite frame tras frame en el camino blocked sin dibujar nada.
- Si A es cierta, el log mostrará: `[EXIT] Update:ScreenBegin` pero NO `[EXIT] Update:FrameBeginOK`. El resumen `[C3D] frame N` aparecerá solo si el cuelgue ocurre en los frames 0-11 (T3: resumen solo frames 0-11). Si es en frame >11, solo las marcas `[EXIT]` dicen dónde se para.

**Hipótesis B:** `Draw2D()` se cuelga. El frame begin funciona pero algún draw dentro de `Draw2D()` falla y la GPU se queda trabada.
- Si B es cierta, el log mostrará: `[EXIT] Update:FrameBeginOK` pero NO `[EXIT] Update:Draw2Ddone`.

**Hipótesis C:** `C3D_FrameEnd()` o código posterior se cuelga. El dibujo se envía pero la GPU no termina o el código después de FrameEnd se para.
- Si C es cierta, el log mostrará: `[EXIT] Update:Draw2Ddone` pero NO `[EXIT] Update:FrameEnd`.

---

### Segundo objetivo: diagnóstico del menú zoomado y repetido

**Contexto:** El HUD (vida, munición, cara) se ve bien. El menú sale zoomado y repetido arriba y abajo. La prueba 34 dio `scalemode=0, scalefactor=1.00, screen=400x240, vp=0,0,400x240`: valores normales, sin zoom evidente en las variables del motor.

**Hipótesis M1 (zoom):** El menú usa un viewport o dimensiones de F2DDrawer distintas al HUD, causando el estiramiento (zoom). `C3DRenderState::SetViewport(x,y,w,h)` hace `C3D_SetViewport(y, W-x-w, h, w)` (c3d_renderstate.cpp:123-128), con swap de ejes. Si el menú establece un viewport con dimensiones distintas, los píxeles se estiran.
- Si M1 es cierta, el log mostrará `[M-VP]` y/o `[M-F2D]` con valores distintos entre frames con menú y frames con HUD.

**Hipótesis M2 (repetición):** El wrap de la textura del menú está activado (`DTF_Wrap`), haciendo que las coordenadas de textura se repitan en lugar de cortarse. El código en hw_draw2d.cpp:175 pasa `CLAMP_NONE` cuando `DTF_Wrap` está activo, lo que permite que la textura se tilee.
- Si M2 es cierta, el log mostrará `[M-TX] wrap=yes` en frames con menú pero `wrap=no` (o `CLAMP_XY`) en frames con HUD.

**Hipótesis M3 (repetición por coordenadas):** Los vértices del menú usan coordenadas y/o transformaciones (`cmd.transform`) que colocan los dibujos fuera del área visible, y la GPU los envuelve o repite.
- Si M3 es cierta, el log mostrará `[M-VERT]` con coordenadas fuera de rango (mayores que 400x240) en frames con menú, y `[M-XFORM]` con valores de transformación distintos a identidad.

**Marcas de medición del menú (solo lectura, sin cambiar comportamiento):**
En `hw_draw2d.cpp`, dentro de `#ifdef __3DS__`, activas solo en los primeros ~5 frames con menú y ~5 frames con HUD:
1. `[M-CMDS] N` — número de comandos en `commands.Size()` al entrar en `Draw2D()` (l.68).
2. `[M-VP] x,y,w,h` — viewport que recibe `state.SetViewport()` (l.54).
3. `[M-F2D] w,h` — ancho y alto del F2DDrawer (`drawer->GetWidth()`, `drawer->GetHeight()`) (l.55).
4. `[M-VERT] v0,v1,v2,v3` — primeros 4 vértices del primer comando de dibujo (l.89+, dentro del bucle `for(auto &cmd : commands)`).
5. `[M-TX] w,h,wrap` — tamaño de textura y si `DTF_Wrap` está activo, del primer comando con textura válida (l.170-175, dentro de `if (cmd.mTexture != nullptr && cmd.mTexture->isValid())`).
6. `[M-XFORM] a,b,c,d` — matriz de transformación 2x2 del primer comando que la usa (l.147-168, `cmd.transform.Cells`).
Solo lectura de variables locales, sin cambiar el flujo.

**Preguntas de contraste pendientes:** ¿la consola (~) se ve bien?, ¿el automapa?, ¿la pantalla de título? (Confirmar con Alex.)

---

**Única variable que cambia:**
- **Cuelgue:** Cinco marcas `[EXIT]` en `c3d_framebuffer.cpp`, dentro de `#ifdef __3DS__`:
  1. `[EXIT] Update:Enter` — al entrar en `Update()` (antes de l.325).
  2. `[EXIT] Update:ScreenBegin` — tras `ScreenBegin()` (l.325).
  3. `[EXIT] Update:FrameBeginOK` — tras verificar que `!mFrameBlocked` (l.326-327).
  4. `[EXIT] Update:Draw2Ddone` — tras `Draw2D()` (l.340).
  5. `[EXIT] Update:FrameEnd` — tras `C3D_FrameEnd(0)` (l.343).
- **Menú:** 6 marcas `[M-*]` en `hw_draw2d.cpp`, dentro de `#ifdef __3DS__`, activas solo en los primeros ~5 frames con menú y ~5 frames con HUD.
- Solo `N3DS_Log` + `N3DS_LogFlush`, sin cambios funcionales.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | Esperar al título (~100 s). Abrir menú (Enter/E). Esperar 10 s (se recogen frames con menú). Cerrar menú. Esperar 60 s (se recogen frames con HUD). HOME→volver. Esperar 60 s. HOME→cerrar. Esperar 15 s. Criterio: el log. |

**Marcadores que se quedan:** los de la pregunta abierta (cierre desde HOME, ahora dentro de `D_Display()` → `Update()`). Los de menú-diag de la prueba 34 se mantienen (ya sirven). Las marcas `[M-*]` nuevas se quedan para esta prueba. **Marcadores que se quitan:** los `[EXIT] Before/After` de `d_main.cpp` (prueba 36, pregunta ya respondida: `D_Display()` es la culpable).

**Cómo se compila:** `platform/3ds/docker-build-test.sh 37`.

**Autocomprobación (9 preguntas):**

1. **Tabla de diferencias.** No hay shader ni variantes. Las hipótesis del cuelgue (A, B, C) se distinguen por la última marca `[EXIT]` en el log. Las hipótesis del menú (M1, M2, M3) se distinguen por los valores de `[M-VP]`, `[M-F2D]`, `[M-TX]`, `[M-VERT]`, `[M-XFORM]` comparando frames con menú vs HUD. No hay pasadas con archivos de control que cambien el comportamiento.

2. **¿Cada hipótesis predice algo distinto en el log?** Sí, para el cuelgue: A se corta tras `[EXIT] Update:ScreenBegin` (sin `FrameBeginOK`), B tras `[EXIT] Update:FrameBeginOK` (sin `Draw2Ddone`), C tras `[EXIT] Update:Draw2Ddone` (sin `FrameEnd`). Para el menú: M1 predice `[M-VP]` y/o `[M-F2D]` distintos, M2 predice `[M-TX] wrap=yes` solo en menú, M3 predice `[M-VERT]` fuera de rango y `[M-XFORM]` distinta de identidad.

3. **¿Lo que dices que se verá en pantalla puede verse?** Para el cuelgue, no: el criterio es el log. Para el menú, la pantalla ya muestra el problema conocido (zoomado y repetido), pero el diagnóstico viene del log.

4. **¿Se resuelve en una sola compilación?** Sí: una pasada recoge tanto las marcas `[EXIT]` como `[M-*]`. El cuelgue se diagnostica al final (HOME→cerrar) y el menú se mide al abrirlo (~10 s antes).

5. **¿Cada afirmación sobre una API tiene su fuente?** Sí: `C3D_FrameBegin` (citro3d `renderqueue.c`, hechos-3ds.md F1-F2), `C3D_FrameEnd` (ídem, F3-F4), `ScreenBegin()` (c3d_framebuffer.cpp:142-168), `Update()` (c3d_framebuffer.cpp:323-349), camino blocked (l.326-336), `Draw2D()` (hw_draw2d.cpp:50-74), `SetViewport` (c3d_renderstate.cpp:123-128), `SetMaterial` con wrap (hw_draw2d.cpp:170-176), `cmd.transform` (hw_draw2d.cpp:147-168), MENU-DIAG (d_main.cpp:1281-1306).

6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Para el cuelgue: si aparece `[EXIT] Update:FrameEnd`, el cuelgue es después de `Update()`: ¿`twod->OnFrameDone()`? ¿`S_UpdateMusic()`? Si no llega ni a `Update:Enter`, es antes de `screen->Update()` en `End2DAndUpdate()`. Para el menú: si `[M-VP]`, `[M-F2D]`, `[M-TX]`, `[M-VERT]`, `[M-XFORM]` son idénticos entre menú y HUD, el problema no está en estos valores y hay que mirar más abajo (shader, textura, o cómo se interpretan las coordenadas).

7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí: una sola pasada, sin archivos de control. El orden de acciones está claro en la tabla.

8. **¿Lo que vas a añadir ya existe?** Sí: `N3DS_Log` y `N3DS_LogFlush` ya existen. Las marcas `[EXIT]` y `[M-*]` son nuevas pero no entran en conflicto con nada. No se cambia el comportamiento ni el camino de salida.

9. **¿Qué se ejecuta después de tu cambio?** Cada marca tiene `N3DS_LogFlush()` con `fsync`. Flujo: `D_Display()` → `End2DAndUpdate()` → `screen->Update()` → marcas `[EXIT]` dentro de `Update()` → si todo va bien, `[EXIT] D_Display done` (ya existe en d_main.cpp). Las marcas `[M-*]` se emiten dentro de `Draw2D()` (l.50-74) en los primeros frames con menú y HUD.

## Tabla de la revisora

La imagen está corregida (prueba 32). HOME suspende/restaura (prueba 33). `D_Display()` se cuelga al cerrar desde HOME (pruebas 35 y 36). Falta: diagnosticar qué paso dentro de `D_Display()` → `C3DFrameBuffer::Update()` se cuelga. También diagnosticar el zoom del menú.

**Prioridades (en orden):**
1. **Cierre desde HOME:** `D_Display()` se cuelga (prueba 36). Diagnosticar qué paso dentro de `C3DFrameBuffer::Update()` es la causa (prueba 37).
2. **Zoom del menú:** `scalemode=0` (estirar), `scalefactor=1.00`, `screen=400x240` (prueba 34). Medir valores reales del frame con menú vs HUD (prueba 37).
3. **Rendimiento en partida:** sin datos aún.
4. **Carga lenta (~97 s):** `Texman.Init` tarda ~52 s (53%).
5. **Demos y mundo 3D:** sin datos.

Uniformes del shader del motor (valores del log): `mvp=0`, `colorscale=4`, `colormul=5`, `texmat=6`. DVLB con 1 DVLE.

## Prueba 29 (hecha, 4 variantes, logs analizados)

**Pregunta:** ¿Cuál de las dos diferencias con citro3d (fuente de dp4: `r2` vs `intex` directo; componentes escritos: solo `.xy` vs `.xy` + `.zw`) es la que causa el cuelgue?

**Resultado:**

| Var | dp4 source | outtc0.zw | triangle= | Log | Resultado |
|-----|-----------|-----------|-----------|-----|-----------|
| V0 | `intex` directo | no | 0 | 70 líneas, HANG frame 1 | ❌ HANG |
| V1 (1.ª) | `r2` (original) | sí | 0 | 36 líneas, no llegó a C3D | ❌ Log incompleto → **no cuenta** |
| V1 (repetida) | `r2` (original) | sí | 0 | 1767 líneas, frames 0-11 ok, `Init complete` | ✅ Funciona |
| V2 | `intex` directo | sí | 0 | frames 0-11 ok, lento ~2min | ✅ Funciona |
| V3 | `r2` (original) | no | 0 | 67 líneas, HANG frame 1 | ❌ HANG (baseline) |

**Archivos de control en cada pasada:** TODAS con `triangle=0`: sin `c3d-triangle.txt`, es decir, **juego real** (pantalla de título, ID Software). V1 repetida con `c3d-v1.txt`, V2 con `c3d-v2.txt`, V3 con `c3d-v3.txt`, V0 sin variante (o `c3d-v0.txt`).

**Veredicto de hipótesis:**

| Hipótesis | Resultado | Razón |
|---|---|---|
| A: Es `r2` vs `intex` directo | ❌ Descartada | V0 (intex directo, sin zw) HANG igual. |
| B: Faltan `.zw` | ✅✅ **Confirmada** | V0 (sin zw) HANG, V1 repetida (con zw) OK, V2 (con zw) OK, V3 (sin zw) HANG. Escribir `outtc0.zw` es la diferencia entre OK y HANG. |
| C: Hace falta ambos juntos | ❌ Descartada | V2 funciona pero V0 no: no necesita ambos, solo zw. |
| D: Problema más profundo | ❌ Descartada | V1 repetida y V2 funcionan con el juego real. |

**Error en el análisis anterior:** La primera V1 dio un log incompleto (no llegó a C3D). Se usó ese log para concluir "faltar escribir zw es la causa". Por la regla 4b de `plantilla-prueba.md`, un log incompleto no es un resultado. La repetición de V1 (log-V1-repetida.txt) sí es válida: `v29=1`, frames 0-11 OK, `Init complete`. La conclusión se mantiene pero solo con la repetición + V0 + V2 + V3.

**Conclusión:** **Faltar escribir los componentes `.zw` de `outtc0` es la causa del cuelgue.** La Pica200 necesita que todos los componentes del output de texcoord estén definidos. Cuando solo se escriben `.x` y `.y` (por instrucciones separadas `dp4 outtc0.x, ...` y `dp4 outtc0.y, ...`), los componentes `.z` y `.w` quedan indefinidos y la GPU se cuelga. Añadir `mov outtc0.zw, ones` (o cualquier constante) lo arregla. Funciona tanto con `r2` como con `intex` directo como fuente de dp4.

**Orientación del triángulo:** V0: negra (HANG). V1 (1.ª): negra (no llegó a C3D). V1 (repetida): pantalla de título del juego + letras de ID Software. V2: lo mismo (lento, ~2 min). V3: negra (HANG).

## Prueba 26 (hecha, con 2 pasadas)

**Resultado:**
- **A** (`c3d-triangle.txt` + `c3d-minimal-shader.txt`): `minshader=1`, frames 0-11 `begin=ok`, sin `GPU HANG`. Triángulo visible en pantalla. ✅ **El shader de simple_tri funciona.**
- **B** (`c3d-triangle.txt` solo): `minshader=0`, `GPU HANG` en frame 1. ❌ **El shader del motor cuelga.**

**Conclusión:** El problema es el shader del motor (`c3d_main.v.pica`) o cómo se configuran sus uniformes. El render target, la GPU y citro3d están sanos.

## Prueba 27 (hecha, 5 pasadas, logs analizados)

**Pregunta:** ¿Qué aspecto concreto del shader del motor (código, instrucciones extra, output de texcoord, o datos) causa el cuelgue de GPU?

**Resultado:**

| Var | Shader | `outtc0` | mul | Uniformes | v27= | Log | Resultado |
|-----|--------|----------|-----|-----------|------|-----|-----------|
| V0 | c3d_main (full) | ✅ | ✅ | 8 | 0 | 70 líneas | ❌ HANG |
| V1 | c3d_nouttc0 | ❌ | ✅ | 8 | 1 | 4630 líneas, frames 0-11 ok | ✅ OK |
| V2 | c3d_nomul | ✅ | ❌ | 8 | 2 | 70 líneas | ❌ HANG |
| V3 | c3d_intermed | ❌ | ❌ | 8 | 3 | 2386 líneas, frames 0-11 ok | ✅ OK |
| V4 | simple_tri | ❌ | ❌ | 4 | 4 | 1400 líneas, frames 0-11 ok | ✅ OK |

**Veredicto de hipótesis:**

| Hipótesis | Resultado | Razón |
|---|---|---|
| H1: instrucciones extra | ✅ Confirmada (parcial) | V1 y V3 funcionan con menos instrucciones que V0. Pero V2 tiene igual instrucciones que V1 y se cuelga: no son las instrucciones en sí, sino cuáles. |
| H2: `outtc0` es la causa | ✅✅ **Fuertemente confirmada** | V1 (sin outtc0) OK, V2 (con outtc0) HANG, V0 (con outtc0) HANG, V3 (sin outtc0) OK. Es la única variable que separa HANG de OK en todos los casos. |
| H3: `mul` del color | ❌ Descartada | V2 quita el mul pero se cuelga (tiene outtc0). |
| H4: 8 uniformes | ❌ Descartada | V3 tiene 8 uniformes y funciona. |
| H5: datos del motor | ❌ Descartada | V4 usa datos del motor con shader simple_tri y funciona. |

**Conclusión:** El output de coordenadas de textura (`outtc0`) es la causa del cuelgue. Cuando el shader escribe `outtc0`, la GPU se cuelga. Cuando no lo escribe, funciona.

**Orientación del triángulo:** En la prueba 26 (simple_tri) el triángulo apuntaba hacia arriba porque usaba `Mtx_OrthoTilt` con coordenadas de pantalla (vértices en y=40-200, x=100-300). En la prueba 27 (V1-V4) apunta a la izquierda porque usa la matriz identidad con vértices en clip space (-0.5 a 0.5): el vértice superior está en (0, 0.5) y la base en (-0.5, -0.5) a (0.5, -0.5), que en pantalla se ve como un triángulo con la punta a la izquierda.

**Autocomprobación (plantilla-prueba.md, pregunta 6):**
6. ¿Si todas las variantes dieran el mismo resultado, qué sabría? Si V0 y V2 funcionaran ambas, sabría que ni `outtc0` en sí ni `dp4` son el problema: sería la combinación de `dp4` + falta de textura en el tex env. Eso descartaría A y B, y confirmaría que el arreglo es bindar una textura.

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
| 35 | ¿Dónde se para el cierre desde HOME? | ✅ Entre `TryRunTics done` y `D_Display done`: `I_StartTic()`, `D_ProcessEvents()` o `D_Display()`. Hipótesis C descartada (el bucle llega a la iteración). |
| 36 | ¿Cuál de las 3 funciones se cuelga? | ✅ `D_Display()` es la culpable. `Before D_Display` presente, `D_Display done` ausente. I_StartTic y D_ProcessEvents completan sin problemas. |

## Prueba 30 (hecha, sin archivos de control)

**Pregunta:** ¿El arreglo permanente (`mov outtc0.zw, ones`) hace que el juego arranque sin archivos de control? ¿Dónde está el cuello de botella de ~2 min?

**Resultado:** Log de 76 líneas, `Test: test 30`. Sin GPU HANG. Frames 0-11 OK. `Init complete` en línea 55.

**Lo que se ve en pantalla:** Pantalla de título de Doom II y la de créditos de id Software se alternan. La imagen está **volteada verticalmente** (arriba↔abajo): letras cabeza abajo, texto legible izquierda→derecha, logo de DOOM II abajo. Colores correctos.

**Tiempos de carga:** sin medir correctamente. Los números entre corchetes del log son KB de heap, no milisegundos. `N3DS_ElapsedMs()` dividía los ticks entre 1000 en vez de `SYSCLOCK_ARM11/1000` (268111). El valor `T+26331644 ms` era ~98 s reales multiplicados por ~268. Arreglado para la prueba 31.

**Demo playback:** sin datos. El log solo resume los 12 primeros fotogramas.

## Prueba 31 (hecha, 2 pasadas, logs analizados)

**Pregunta:** ¿El volteo vertical de la imagen está en la geometría (viewport, coordenadas de vértice) o en las texturas (orden de filas al copiar)?

**Resultado:**

| Pasada | Switch | testflip= | Log | Líneas | Resultado |
|---|---|---|---|---|---|
| V0 | `c3d-testflip.txt` | 1 | log-V0.txt | 77 | ✅ Cuadros en posición correcta |
| V1 | ninguno | 0 | log-V1.txt | 76 | ✅ Título + créditos alternándose, volteados |

**V0** (testflip=1): En pantalla, cuadrado rojo arriba a la izquierda y cuadrado verde abajo a la izquierda. En código (línea 52 del log): rojo en (0,0,60,60), verde en (0,180,60,60). La coordenada Y=0 aparece arriba y Y=180 abajo. **Y crece hacia abajo: correcto.**

**V1** (testflip=0): Igual que la prueba 30: pantalla de título y créditos de id Software alternándose, volteadas verticalmente. Sin GPU HANG.

**Tiempos de carga (con timestamps corregidos):**

| Fase | V0 (s) | V0 (%) | V1 (s) | V1 (%) |
|---|---|---|---|---|
| Texman.Init | 51.1 | 52.9% | 51.1 | 52.7% |
| Key bound gap (bumplight) | 13.6 | 14.1% | 13.6 | 14.0% |
| M_LoadDefaults | 10.1 | 10.4% | 10.4 | 10.7% |
| LoadActors | 7.0 | 7.3% | 6.7 | 6.9% |
| GameMain start | 3.8 | 3.9% | 3.8 | 3.9% |
| D_CheckNetGame | 3.2 | 3.4% | 3.6 | 3.7% |
| Total | 96.7 s | | 97.0 s | |

**Conclusión (tres columnas):**

| Demostrado | Probable | Sin datos |
|---|---|---|
| El pipeline geométrico conserva Y correctamente: (0,0) arriba, (0,180) abajo (V0, log línea 52, fotos) | El volteo está en `CreateTexture` (c3d_hwtexture.cpp): las filas de la textura se copian en orden incorrecto | Alex no confirmó su tiempo manual de carga para comparar con ~97 s de timestamps |
| Texman.Init tarda ~51 s (53% del total), es el cuello de botella claro (ambos logs, línea 31) | El hueco de 13.6 s entre binding de teclas y Texman.Init es parsing de ZScript o carga de definiciones de actor | Por qué Texman.Init es tan lento; qué hace exactamente dentro |
| El juego sigue sin GPU HANG con el arreglo permanente (V1, log 76 líneas, `Init complete` línea 55) | | Si el botón HOME respondió (Alex no lo reportó) |

**Veredicto:** El volteo vertical **no** está en la geometría. Está en las texturas. La siguiente prueba debe corregir el orden de filas en `CreateTexture` (`c3d_hwtexture.cpp`).

## Prueba 32 (hecha, 1 pasada, log analizado)

**Pregunta:** ¿El flip de filas en `CreateTexture` (`sy = h - 1 - y * h / ph`) corrige la imagen volteada?

**Resultado:**

| Pasada | Cambio | Log | Líneas | Resultado |
|---|---|---|---|---|
| Única | Flip de textura + testflip removed | log.txt | 76 | ✅ Imagen correcta |

**En pantalla:** Logo de DOOM II arriba, créditos en orden, letras derechas, colores correctos. Sin volteo vertical.

**Tiempos de carga:**

| Fase | Tiempo (s) | % |
|---|---|---|
| Texman.Init | 52.1 | 53.4% |
| Key bound gap (bumplight) | 14.0 | 14.4% |
| M_LoadDefaults | 9.9 | 10.2% |
| LoadActors | 6.7 | 6.9% |
| GameMain start | 3.8 | 3.9% |
| D_CheckNetGame | 3.2 | 3.3% |
| Total | 97.6 s | |

Alex cronometró [X] minutos manualmente. El timestamp del log da ~97.6 s (~1 min 38 s).

**Conclusión (tres columnas):**

| Demostrado | Probable | Sin datos |
|---|---|---|
| El flip de textura corrige la imagen por completo (log 76 líneas, foto confirma título + créditos correctos) | Texman.Init sigue siendo el cuello de botella (~52 s, 53%) | Por qué Texman.Init es tan lento; qué hace dentro |
| El pipeline geométrico + flip de textura dan imagen perfecta (pruebas 31+32) | El hueco de 14 s entre binding de teclas y Texman.Init es parsing de ZScript/carga de definiciones | Si el tiempo manual de Alex coincide con ~97 s |
| HOME sigue sin responder (Alex lo confirma) | No hay `aptMainLoop()` en el bucle: `aptHandleJumpToHome()` nunca se ejecuta | Si `hidScanInput()` se necesita aparte de SDL para los mandos |

**Código:** Se quitó el código testflip de `c3d_framebuffer.cpp` (15 líneas) y se aplicó el flip en `c3d_hwtexture.cpp` línea 100.

## Prueba 34 (hecha, 1 pasada, log analizado)

**Pregunta principal:** ¿Conectar `aptMainLoop()` → `false` con `gameloop_abort = true` hace que el juego salga correctamente al pulsar HOME para cerrar?

**Segunda pregunta (diagnóstico del menú, solo medir):** ¿Qué valores de escala y tamaño se usan cuando se dibuja el menú?

**Resultado:**
- Log: 107 líneas, `Test: test 34`, commit 33a453edc.
- Carga: ~97 s (T+97387 ms, línea 46). Texman.Init ~52 s (53%). Sin GPU HANG, frames 0-11 OK.
- `[MENU-DIAG]` (línea 77): `scalemode=0 scalefactor=1.00 screen=400x240 client=400x240 twod=400x240 clean=1x1 uiscale=0 vp=0,0,400x240 lb=0,0,400x240`
- `[APT] suspend` (línea 105): HOME suspendió correctamente.
- `[EXIT] aptMainLoop returned false` (línea 106): `aptMainLoop()` sí devuelve `false` al pedir cierre.
- `[EXIT] gameloop_abort set` (línea 107): `gameloop_abort` se setea a `true`.
- **El log se corta aquí.** No aparece ninguna marca posterior.
- Alex confirma: "se queda en 'Cerrando el programa suspendido'".

**Marcas [EXIT] que aparecen:** `[EXIT] aptMainLoop returned false` (l.106), `[EXIT] gameloop_abort set` (l.107).

**Marcas [EXIT] que NO aparecen (log se corta):** ninguna marca después de l.107. No se ve `quickexit`, salida del bucle, limpieza del backend, ni `GameMain exit`.

**Análisis:** `I_StartFrame()` setea `gameloop_abort` → el bucle `for(;;)` de `D_DoomLoop()` sigue (`TryRunTics`, `D_Display`, etc.) → llega a `if (gameloop_abort)` (d_main.cpp:1651) → ejecuta `C_DoCommand("quickexit")` → `M_Quit()` solo limpia sonido/pantalla, **no sale del bucle** → el bucle continúa → siguiente iteración llama `I_StartFrame()` otra vez → `aptMainLoop()` vuelve a devolver `false` → bucle infinito. `quickexit` no rompe el bucle.

**Conclusión (tres columnas):**

| Demostrado | Probable | Sin datos |
|---|---|---|
| `aptMainLoop()` devuelve `false` al cerrar desde HOME (l.106) | El log se corta porque el sistema mata el proceso tras el suspend | Si el bucle llegó a ejecutar `quickexit` (no hay marca después de l.107) |
| `gameloop_abort` se setea (l.107) | `quickexit` se ejecuta pero no rompe el bucle: `M_Quit()` solo limpia, no hace `return` ni `break` | Cuánto tiempo pasó entre l.107 y el corte |
| HOME suspende y restaura (l.105, Alex confirma) | | |
| Menú: `scalemode=0`, `scalefactor=1.00`, `screen=400x240` (l.77) | | |
| Imagen correcta, sin GPU HANG (frames 0-11 OK) | | |

## Prueba 35 (hecha, 1 pasada, log analizado)

**Pregunta:** ¿Dónde exactamente se para el camino de salida al pulsar HOME para cerrar?

**Resultado:**
- Log: 81 líneas, `Test: test 35`, commit 886383d51.
- Sin GPU HANG, frames 0-11 OK, `Init complete` (l.55). Carga ~97 s.
- Marcas `[EXIT]` en orden: `[EXIT] aptMainLoop returned false` (l.79), `[EXIT] gameloop_abort set` (l.80), `[EXIT] TryRunTics done` (l.81).
- **Última marca presente:** `[EXIT] TryRunTics done` (d_main.cpp:1648).
- **Primera marca ausente:** `[EXIT] D_Display done` (d_main.cpp:1658).

**Código entre la última marca presente y la primera ausente** (d_main.cpp:1652-1654):
```
I_StartTic ();       // l.1652: llama hidScanInput() en 3DS
D_ProcessEvents();   // l.1653: procesa eventos de input
D_Display ();        // l.1654: renderiza el fotograma (llama al backend GPU)
```

**Análisis:** El bucle llegó a la iteración donde `gameloop_abort` es `true` (Hipótesis C descartada). `TryRunTics()` completó (l.1648 en log). El fallo está entre `TryRunTics done` y `D_Display done`: una de las tres funciones `I_StartTic()`, `D_ProcessEvents()`, o `D_Display()` es donde se para. `D_Display()` es la más pesada (renderizado GPU completo) y ya sabemos que el backend GPU funciona (frames 0-11 OK), pero el contexto puede haber cambiado tras el suspend/restore de APT.

**Conclusión (tres columnas):**

| Demostrado | Probable | Sin datos |
|---|---|---|
| El bucle llega a la iteración con `gameloop_abort=true` (l.81: `TryRunTics done`) | `D_Display()` es la función que se cuelga: es la más pesada y hace llamadas al backend GPU tras un suspend/restore de APT | Cuál de las 3 funciones (`I_StartTic`, `D_ProcessEvents`, `D_Display`) es exactamente la culpable |
| Hipótesis C descartada: el proceso no se suspende antes de la siguiente iteración (l.81 aparece) | El contexto GPU puede estar corrupto tras el restore de APT (suspend l.78 → aptMainLoop false l.79) | Si `hidScanInput()` en `I_StartTic()` falla tras el APT restore |
| El camino de salida está dentro de D_DoomLoop(), no antes (l.79-81 en orden) | | Si el log se corta por fsync fallido o por timeout del sistema |



## Prueba 36 (hecha, 1 pasada, log analizado)

**Pregunta:** ¿En qué función exacta (`I_StartTic`, `D_ProcessEvents` o `D_Display`) se para el cierre desde HOME?

**Resultado:**
- Log: 91 líneas, `Test: test 36`, commit 3c9051003 + uncommitted changes.
- Sin GPU HANG, frames 0-11 OK, `Init complete` (l.55). Carga ~133 s.
- Marcas `[EXIT]` en orden (línea 91, todas pegadas): `TryRunTics done` → `Before I_StartTic` → `After I_StartTic` → `Before D_ProcessEvents` → `After D_ProcessEvents` → `Before D_Display`.
- **Última marca presente:** `[EXIT] Before D_Display` (d_main.cpp:1681).
- **Primera marca ausente:** `[EXIT] D_Display done` (d_main.cpp:1688).

**Veredicto de hipótesis:**

| Hipótesis | Resultado | Razón |
|---|---|---|
| A: `I_StartTic()` se cuelga | ❌ Descartada | `After I_StartTic` aparece en el log. |
| B: `D_ProcessEvents()` se cuelga | ❌ Descartada | `After D_ProcessEvents` aparece en el log. |
| C: `D_Display()` se cuelga | ✅✅ **Confirmada** | `Before D_Display` aparece pero `D_Display done` no. |

**Conclusión (tres columnas):**

| Demostrado | Probable | Sin datos |
|---|---|---|
| `D_Display()` es la función que se cuelga en el cierre desde HOME (log l.91: `Before D_Display` presente, `D_Display done` ausente) | El contexto GPU está corrupto tras el APT suspend/restore: los frames 0-11 funcionan pero tras el suspend (l.88) el estado puede haber cambiado | Qué dentro de `D_Display()` se cuelga exactamente (¿`C3D_FrameBegin`, `DrawElements`, otro?) |
| `I_StartTic()` y `D_ProcessEvents()` completan sin problemas (l.91) | El problema es similar al GPU HANG de las pruebas 16-23 pero activado por el APT suspend | Si `fsync` escribe todo antes del cuelgue (la línea 91 tiene 7 marcas pegadas: el log se guarda) |

**Nota:** Las 7 marcas de la línea 91 están pegadas sin separador porque cada `N3DS_Log()` no añade `\n` y `N3DS_LogFlush()` solo vacía el buffer. Es comportamiento esperado.

**Lo que se pidió:**
- [x] Aislar cuál de las 3 funciones (`I_StartTic`, `D_ProcessEvents`, `D_Display`) se cuelga en el cierre desde HOME.

**Única variable que cambia:** Cinco marcas `[EXIT]` adicionales en `d_main.cpp` (l.1652-1684), dentro de `#ifdef __3DS__`. Solo `N3DS_Log` + `N3DS_LogFlush`, sin cambios funcionales.

**Pasadas en la consola:**

| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | Esperar al título (~100 s), HOME→volver, esperar 60 s, HOME→cerrar, esperar 15 s. Criterio: el log. |

**Marcadores que se quedan:** los de la pregunta abierta (cierre desde HOME, ahora dentro de `D_Display()`). **Marcadores que se quitan:** ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 36`.

**Autocomprobación (9 preguntas):**

1. **Tabla de diferencias.** No hay shader ni variantes. Las 3 hipótesis se distinguen por la última marca `[EXIT]` que aparece en el log.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí: A se corta tras `Before I_StartTic`, B tras `Before D_ProcessEvents`, C tras `Before D_Display`.
3. **¿Lo que dices que se verá en pantalla puede verse?** No: el criterio es el log.
4. **¿Se resuelve en una sola compilación?** Sí: una pasada con las 5 marcas nuevas + la existente distingue las 3 hipótesis.
5. **¿Cada afirmación sobre una API tiene su fuente?** Sí: `hidScanInput()` (libctru `hid.c` línea 172-219), `D_ProcessEvents()` (d_event.cpp:61-113), `D_Display()` (d_main.cpp:1255+).
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Si apareciera `D_Display done`, sabría que el cuelgue es después de `D_Display()`.
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí: una sola pasada, sin archivos de control.
8. **¿Lo que vas a añadir ya existe?** Sí: `N3DS_Log` y `N3DS_LogFlush` ya existen.
9. **¿Qué se ejecuta después de tu cambio?** Cada marca tiene `N3DS_LogFlush()` con `fsync`.


- **Texto en ruso:** línea 24 de los logs: `В игрока %o попала межпространственная слизь!` se pega con `Selecting OpenGL backend...` sin separador. Causa desconocida.
- **HOME no responde:** en ninguna de las pasadas el botón HOME volvió al menú de la 3DS. En la pasada A del test 26 los fotogramas avanzaban sin cuelgue y tampoco respondió, así que no es por el cuelgue de GPU. Pendiente de investigar.
- Entrada de mandos: el gestor del joystick está vacío (punto 2).
- Líneas y puntos (automapa), niebla, luz, portales, sonido, controles 3DS, pantalla de abajo, `.cia` (unique ID 0xF3D5C).
- La carga de las fuentes TrueType latinas genera atlas de 1 MB cada una con el juego de caracteres completo: lento y caro en memoria. Recortar el juego de caracteres cuando el resto funcione.
