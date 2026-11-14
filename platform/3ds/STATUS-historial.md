# UZDoom 3DS: historial de pruebas cerradas

Lo que salió de `STATUS.md` el 13 nov. 2026 para que se lea menos al empezar. Es historia: lo vigente está en `STATUS.md`. La plantilla completa de la prueba 37 antes de separarla (con el menú) está en el commit `a6257348d`.

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

## Lo que sabíamos hasta la prueba 36 (lista antigua)

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

## Tabla de la revisora (prueba 37, versión con el menú)

La imagen está corregida (prueba 32). HOME suspende/restaura (prueba 33). `D_Display()` se cuelga al cerrar desde HOME (pruebas 35 y 36). Falta: diagnosticar qué paso dentro de `D_Display()` → `C3DFrameBuffer::Update()` se cuelga. También diagnosticar el zoom del menú.

**Prioridades (en orden):**
1. **Cierre desde HOME:** `D_Display()` se cuelga (prueba 36). Diagnosticar qué paso dentro de `C3DFrameBuffer::Update()` es la causa (prueba 37).
2. **Zoom del menú:** `scalemode=0` (estirar), `scalefactor=1.00`, `screen=400x240` (prueba 34). Medir valores reales del frame con menú vs HUD (prueba 37).
3. **Rendimiento en partida:** sin datos aún.
4. **Carga lenta (~97 s):** `Texman.Init` tarda ~52 s (53%).
5. **Demos y mundo 3D:** sin datos.

Uniformes del shader del motor (valores del log): `mvp=0`, `colorscale=4`, `colormul=5`, `texmat=6`. DVLB con 1 DVLE.

## Prueba 37 (cerrada, 13 nov. 2026)

**Pregunta:** ¿Qué paso de `D_Display()` → `C3DFrameBuffer::Update()` no vuelve al cerrar desde HOME?

**Log analizado:** 92 líneas. `Test:` confirma prueba 37.

**Última marca presente:** `[EXIT] f2016:FB_before` (línea 92 del log).
**Primera marca ausente:** `[EXIT] f2016:FB_after`.

**Código entre marcas** (`c3d_framebuffer.cpp`):
- Línea 162-165: marca `FB_before` + `N3DS_LogFlush()`
- Línea 167: `C3D_FrameBegin(C3D_FRAME_SYNCDRAW | C3D_FRAME_NONBLOCK)` ← AQUÍ SE QUEDA
- Línea 170-174: marca `FB_after` (nunca llega)

**Hipótesis:**
| Hipótesis | Resultado | Línea del log |
|---|---|---|
| **S:** `ScreenBegin()` no vuelve (`C3D_FrameBegin` o `gspWaitForVBlank`) | ✅ **CONFIRMADA** | Línea 92: `FB_before` presente, `FB_after` ausente |
| **A1:** `C3D_FrameBegin` falla reintentos, camino bloqueado se para | ❌ Descartada | No sale `FB_fail` ni `blk_*` |
| **B:** `ScreenBegin()` termina, camino normal se para en `Draw2D()`/`FrameEnd()` | ❌ Descartada | No sale `SB_ok` |
| **C:** `Update()` termina, se para después en `OnFrameDone()` | ❌ Descartada | No sale `upd_end` |

**Conclusión (tres columnas):**

| Demostrado | Probable | Sin datos |
|---|---|---|
| `C3D_FrameBegin(C3D_FRAME_SYNCDRAW \| C3D_FRAME_NONBLOCK)` bloquea indefinidamente al cerrar desde HOME, incluso con el flag `NONBLOCK` | El problema está dentro de `C3D_FrameBegin`, en la espera de que termine el trabajo GPU del frame anterior (`gspWaitForEvent` interno) | Qué dentro de `C3D_FrameBegin` bloquea exactamente (citro3d es código cerrado en parte); si liberar recursos GPU antes de llamar a `C3D_FrameBegin` evitara el bloqueo |

## Prueba 38 (cierre desde HOME: ¿sin SYNCDRAW vuelve?)

**Pregunta:** ¿Tras la orden de cerrar, el juego vuelve al menú HOME si `C3D_FrameBegin` no espera las vblank?

**Resultados:**
- **Log A** (sin SYNCDRAW): `FB_after` ✅ → `C3D_FrameBegin` volvió. `D_Display done` ✅. Toda la cadena de salida hasta `CExitEvent caught` ✅. Última marca: `D_Cleanup start`. Se para **dentro** de `D_Cleanup()`.
- **Log B** (saltando `D_Display()`): `skip D_Display` → toda la cadena completa hasta `GameMain exit 0` ✅. Crash dump (luma/dumps/arm11): PC=0, LR=2, CPSR=0 → crash **post-main()** en ARM11.

**Conclusiones por hipótesis:**
| Hipótesis | Resultado |
|---|---|
| **A (vblank dejan de llegar):** sin `SYNCDRAW`, `C3D_FrameBegin` vuelve | ✅ **CONFIRMADA** (Log A: `FB_after` presente) |
| **B (NONBLOCK no espera):** descartada por código (F2) | — |
| **C (problema más allá de `C3D_FrameBegin`):** el camino de salida se para en otro sitio | ✅ **CONFIRMADA** (Log A: se para en `D_Cleanup`; Log B: crash post-main) |

**Conclusión (tres columnas):**

| Demostrado | Probable | Sin datos |
|---|---|---|
| Sin `SYNCDRAW`, `C3D_FrameBegin` vuelve (`FB_after`). En la pasada A, entre `D_Cleanup start` y `GameMain returning` estaba todo `D_Cleanup()` + las ~12 llamadas post-`D_Cleanup()` de `GameMain`: alguna de esas no vuelve. En la B (saltando `D_Display`) llega a `GameMain exit 0` pero hay crash post-main en ARM11 | `D_Display()` en el último frame del cierre envía trabajo a la GPU que no termina, y al intentar limpiar la GPU (`I_ShutdownGraphics()` → `C3D_Fini()`) se queda esperando indefinidamente | Qué función exacta no vuelve en la pasada A; qué recurso no liberado causa el crash post-main de la pasada B |

## Prueba 39 (¿dónde se para dentro de `D_Cleanup()`?)

**Lo que se pidió:**
- [x] Prueba 39 = dentro de `D_Cleanup()` (con marcas)
- [x] Pregunta: ¿qué función de `D_Cleanup()` no vuelve?
- [x] Marca por cada llamada, no por grupos
- [x] Nombre de la función en la marca
- [x] Plantilla escrita

**Pregunta:** ¿Qué función dentro de `D_Cleanup()` se queda sin volver al cerrar desde HOME?

**Lo que ya sabemos:**
- Punto 12: Log A (prueba 38) última marca `D_Cleanup start`; Log B llega a `GameMain exit 0` pero crash post-main.
- `D_Cleanup()` (d_main.cpp:4541-4619) llama a ~30 funciones.

**Hipótesis:** `D_Cleanup()` se para en una función concreta. Con una marca `[EXIT]` después de cada llamada, la marca ausente dice cuál.

**Marcas (una por llamada, en orden):** 35 marcas `[EXIT] DC after <función>` + `[EXIT] DC end`.

**Resultado:** El log (91 líneas) contiene **las 35 marcas completas**, incluyendo `[EXIT] DC end`. `D_Cleanup()` termina todas sus funciones sin fallar.

**Tabla de hipótesis:**

| Hipótesis | Resultado |
|---|---|
| **A (una función de `D_Cleanup()` no vuelve):** marca ausente en el log | ❌ **DESCARTADA** (35/35 marcas presentes, incluyendo `DC end`) |

**Conclusión (tres columnas):**

| Demostrado | Probable | Sin datos |
|---|---|---|
| `D_Cleanup()` completa todas sus funciones al cerrar desde HOME. El fallo está **después** de `D_Cleanup()`, en el código de `GameMain` que sigue: `CloseNetwork()`, `R_Shutdown()`, `I_ShutdownGraphics()`, destructores globales, o el camino de retorno a `main()` | El crash ocurre en la limpieza post-`D_Cleanup()` de `GameMain` (llamadas a `R_Shutdown()`, `I_ShutdownGraphics()`, `I_ShutdownInput()`, etc.) o en destructores globales tras `return 0` de `main()` | Qué función exacta post-`D_Cleanup()` falla; si es el mismo crash post-main ARM11 de la prueba 38B o un cuelgue diferente |

## Prueba 40 (¿dónde se para post-`D_Cleanup()`?)

**Lo que se pidió:**
- [x] Prueba 40 = post-`D_Cleanup()` en `GameMain()` + destructor `~C3DFrameBuffer()`
- [x] Hipótesis principal: `C3D_Fini()` no vuelve (F12)
- [x] Marca antes y después de `C3D_FrameEnd`, `C3D_RenderTargetDelete`, `C3D_Fini`
- [x] Plantilla escrita y revisada

**Pregunta:** ¿El cierre desde HOME se queda en `C3D_Fini()` (F12: espera sin límite a la GPU)?

**Lo que ya sabemos:**
- Punto 12 y prueba 39: `D_Cleanup()` completa sin fallar. El fallo es post-`D_Cleanup()`.
- F12 (nuevo): `C3D_Fini()` espera indefinidamente a la cola de la GPU. Si quedó trabajo que no termina, no vuelve.
- `GameMain()` (d_main.cpp:4510-4532) llama a `I_ShutdownGraphics()` (l.4518) → destructor `~C3DFrameBuffer()` (c3d_framebuffer.cpp:105-121) → `C3D_FrameEnd()` → `C3D_RenderTargetDelete()` → `C3D_Fini()`.

**Hipótesis principal:** `C3D_Fini()` no vuelve porque la GPU tiene trabajo pendiente del último fotograma de `D_Display()` que no puede terminar.

**Si es cierta, la última marca en el log es:** `[EXIT] FB ~before C3D_Fini`.

**Marcas (en orden, post-`D_Cleanup()`):**
`[EXIT] GM after CloseNetwork`, `[EXIT] GM after GC`, `[EXIT] GM after C_DeinitConsole`, `[EXIT] GM after R_DeinitColormaps`, `[EXIT] GM after R_Shutdown`, `[EXIT] GM after I_ShutdownGraphics`
En el destructor `~C3DFrameBuffer()`: `[EXIT] FB ~before C3D_FrameEnd`, `[EXIT] FB ~after C3D_FrameEnd`, `[EXIT] FB ~before C3D_RenderTargetDelete`, `[EXIT] FB ~after C3D_RenderTargetDelete`, `[EXIT] FB ~before C3D_Fini`

**Resultado:** El log (91 líneas, línea completa de marcas) muestra:
- `[EXIT] GM after R_Shutdown` ✅ presente
- `[EXIT] FB ~before C3D_RenderTargetDelete` ✅ presente (última marca)
- `[EXIT] FB ~after C3D_RenderTargetDelete` ❌ ausente (primera ausente)
- `[EXIT] GM after I_ShutdownGraphics`, `[EXIT] FB ~before C3D_Fini` ❌ ausentes

Flujo: `I_ShutdownGraphics()` entró (el destructor se ejecutó), `mFrameStarted` era false (saltó `C3D_FrameEnd`), y `C3D_RenderTargetDelete(mTop)` (c3d_framebuffer.cpp:122) se bloqueó.

El target `mTop` SÍ está enlazado (`C3D_RenderTargetSetOutput` en l.447), por lo que `C3D_RenderTargetDelete` toma la rama `linked` (citro3d `renderqueue.c:365-366`) → `C3D_RenderTargetDetachOutput()`. En l.381 de `renderqueue.c`, esta función llama a `C3Di_WaitAndClearQueue(-1)` cuando `!inFrame`, esperando indefinidamente a la cola GPU.

**Tabla de hipótesis:**

| Hipótesis | Resultado |
|---|---|
| **A (`C3D_Fini()` no vuelve):** última marca `[EXIT] FB ~before C3D_Fini` | ❌ **DESCARTADA** — ni siquiera llega a `C3D_Fini()` |
| **B (`C3D_RenderTargetDelete` bloquea):** última marca `[EXIT] FB ~before C3D_RenderTargetDelete` | ✅ **CONFIRMADA** |

**Conclusión (tres columnas):**

| Demostrado | Probable | Sin datos |
|---|---|---|
| El bloqueo está en `C3D_RenderTargetDelete(mTop)` (c3d_framebuffer.cpp:122). `mFrameStarted` era false, así que `C3D_FrameEnd()` no se llamó. `C3D_RenderTargetDelete` espera a la cola GPU indefinidamente cuando el target está enlazado y `!inFrame` (citro3d `renderqueue.c:381`) | La GPU tiene trabajo pendiente del último frame que no se cerró con `C3D_FrameEnd()` porque `mFrameStarted` era false. La prueba 38B (saltando `D_Display()`) cerraba limpio porque no había trabajo pendiente | Por qué `mFrameStarted` era false al cerrar; si llamar a `C3D_FrameEnd(0)` antes de shutdown resuelve el problema |

## Prueba 41 (arreglo: sin dibujar al cerrar + marcas post-main)

**Lo que se pidió:**
- [x] Prueba 41 = arreglo, no más marcas
- [x] Sin dibujar tras `gameloop_abort`, sin archivo de control
- [x] Diagnóstico del crash post-main (38B): marcas en cada paso de `main()` tras `GameMain()`
- [x] Hook `APTHOOK_ONEXIT` que abre el log en modo añadir
- [x] Mover `fclose(LogFile)` al final de `main()` para que las marcas post-GameMain funcionen
- [x] Una sola pasada

**Pregunta:** ¿Saltar `D_Display()` siempre con `gameloop_abort` hace que el cierre desde HOME complete sin bloquear, y dónde está el crash post-main?

**Lo que ya sabemos:**
- Punto 12 (38B): saltando `D_Display()` el cierre llega a `GameMain exit 0` pero crash post-main (PC=0, LR=2).
- Punto 14 (40): `C3D_RenderTargetDelete` bloquea porque la GPU tenía trabajo pendiente. Sin `D_Display()` no hay trabajo pendiente.
- Punto 11 (37): tras HOME no llegan vblank, `C3D_FrameBegin` con SYNCDRAW bloquea.
- S8 (`hechos-3ds.md`): `APTHOOK_ONEXIT` se llama en `aptExit()`, después de `main()`, y el log ya está cerrado.
- `i_main_3ds.cpp:242` cierra el log antes de `gfxExit()` y `romfsExit()`. Se mueve al final.

**Hipótesis A (el cierre completa, crash en post-main):**
- `fclose` se mueve al final de `main()`, así que las marcas post-GameMain se pueden escribir. El log muestra `[EXIT] after GameMain`, `[EXIT] after SDL_Quit`, `[EXIT] after gfxExit`, `[EXIT] after romfsExit`.
- Si crasha en `SDL_Quit`: última marca `[EXIT] after GameMain`.
- Si crasha en `gfxExit`: última marca `[EXIT] after SDL_Quit`.
- Si crasha en `romfsExit`: última marca `[EXIT] after gfxExit`.
- Si crasha en `APTHOOK_ONEXIT`: el hook abre el log en modo añadir (`N3DS_DATA_DIR "/log.txt"`, `"a"`), escribe `[APT] exit`, cierra. Crash después.

**Hipótesis B (el cierre no completa, cuelgue en destructores):**
- Última marca `[EXIT] after GameMain` o antes. Cuelgue en destructor global o `atexit` dentro de `GameMain()` (antes de llegar a `main()` post-return).

**Única variable que cambia:** (1) bloque `if (gameloop_abort)` en d_main.cpp:1650-1669 reemplazado por salto incondicional de `D_Display()`, (2) marcas en `main()` post-`GameMain()`, (3) `fclose(LogFile)` movido tras `romfsExit()`, (4) hook `ONEXIT` con `fopen` en modo añadir.

**Pasadas en la consola:**
| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| Única | ninguno | Log completo; juego cierra o crasha |

**Marcadores que se quedan:** los `[EXIT]` de `D_Cleanup()` y post-`D_Cleanup()` (pruebas 39-40). Los `[EXIT] FB ~` del destructor.
**Marcadores que se quitan:** el bloque entero `if (gameloop_abort) { stat(c3d-exit-nodisplay.txt) ... }` (d_main.cpp:1650-1669).

**Cómo se compila:** `platform/3ds/docker-build-test.sh 41`

**Resultado:** El log (87 líneas) muestra:
- Todas las marcas `[EXIT]` de `D_Cleanup()` y post-`D_Cleanup()` presentes ✅
- `[EXIT] after GameMain` + `T+124659 ms: GameMain exit 0` ✅
- `[EXIT] after SDL_Quit`, `[EXIT] after gfxExit`, `[EXIT] after romfsExit`, `[EXIT] atexit callback` ✅
- `[APT] exit` (hook ONEXIT) ❌ ausente — el crash es antes del hook
- Tiempo entre `gameloop_abort set` y última marca con timestamp (`T+124659`): ~24 s (desde T+100769 MAP01 hasta T+124659), pero la secuencia de cierre en sí es instantánea (todas las `[EXIT]` en línea 86 sin marcas intermedias)

**Audio:** `S_Init` (T+18232) y `S_InitData` (T+31652) presentes. Sin marcas de NDSP/DSP o SDL audio init explícitas. FluidSynth falló (T+96079), fallback a ADL. El audio del juego no se inició de forma verificable en el log.

**Dump ARM11 (lee-dump.sh):**
| Registro | Valor | Símbolo |
|---|---|---|
| PC | 0x00bc7e18 | `_lseek_r` (Newlib) |
| LR | 0x00bc8d74 | `__libc_lock_release` (Newlib) |
| FAR | 0x00000018 | lectura fallida (dirección casi nula) |
| DFSR | 0x00000005 | Translation fault - Section |
| Proceso | 3dsx_app | ARM11 core 0 |

Crash: data abort en `_lseek_r`, llamado desde `__libc_lock_release`. La dirección de fallo 0x18 sugiere un puntero nulo + desplazamiento en estructura de descriptor de fichero. El crash es **post-main**, tras `[EXIT] atexit callback`.

**Tabla de hipótesis:**

| Hipótesis | Resultado |
|---|---|
| **A (cierre completa, crash post-main):** todas las `[EXIT]` presentes + dump | ✅ **CONFIRMADA** |
| **B (cierre no completa, cuelgue):** última marca antes de post-main | ❌ **DESCARTADA** |

**Conclusión (tres columnas):**

| Demostrado | Probable | Sin datos |
|---|---|---|
| Saltar `D_Display()` con `gameloop_abort` permite que el cierre complete: `D_Cleanup()`, post-cleanup, `SDL_Quit`, `gfxExit`, `romfsExit` y `atexit` ejecutan sin fallar. Crash en ARM11 post-main en `_lseek_r`←`__libc_lock_release` (Newlib), FAR=0x18, Translation fault. | `_lseek_r` se invoca desde código de limpieza del runtime (descriptor de fichero corrupto o liberado prematuramente) durante la transición APT post-main. | Por qué Newlib intenta `_lseek_r` con un puntero inválido post-main; si el hook `APTHOOK_ONEXIT` se llega a ejecutar (no apareció `[APT] exit` en el log); el cortafuegos del audio del HOME es el sistema reintentando NDSP tras el suspend (sin línea del log que lo muestre) |


## Prueba 42 (hecha, 1 pasada, log analizado)

**Pregunta:** ¿Quitar `romfsExit()` de `main()` evita el crash post-main?

**Lo que ya sabemos:**
- Prueba 41: crash en `_lseek_r`←`__libc_lock_release`, FAR=0x18, Translation fault, ARM11, tras `atexit callback`.
- `devoptab_t` (iosupport.h:36-76): `seek_r` en desplazamiento 0x18. FAR=0x18 = leer `seek_r` de puntero nulo.
- `romfsExit()` (libctru, `romfs_dev.c`): desregistra el devoptab de "romfs:". Newlib mantiene FILE* abiertos a archivos .pk3 de romfs:.
- `i_main_3ds.cpp:269`: `romfsExit()` se llama tras `gfxExit()`, antes de `fclose(LogFile)` y `return`.

**Única variable que cambia:** `romfsExit()` (línea 269 de `i_main_3ds.cpp`) comentada con `#if 0 ... #endif`.

**Log:** `/tmp/opencode/uploads/f8a2a9ee-ce85-454d-a71a-52917b63bad5/log.txt`

**Resultado en pantalla:** Consola volvió al menú HOME sin crash. Tardó un poco en cerrar. La música del menú HOME se cortó y volvió como en la 41.

**Marcas del log:**
- `[mem] start: app 126976 KB, heap 72100 KB`
- `T+0 ms: start`, `T+49 ms: GameMain start`
- `[T+  3878] UZDoom version`, `[T+ 97172] C3D: backend ready`
- `[T+ 97445] Init complete`
- Frames 0-11: `begin=ok retries=0`
- `[T+102341] MAP01 - Entryway`
- `[APT] suspend`
- Todas las marcas `[EXIT]` presentes: `gameloop_abort set`, `skip D_Display (always)`, `quickexit called`, `DeleteScreenJob done`, `S_StopAllChannels done`, `S_StopMusic done`, `CleanSWDrawer done`, `before RunEndoom`, `RunEndoom returned`, `CExitEvent caught`, `D_Cleanup start`, todas las `DC after ...`, `DC end`, `GM after ...`, `FB ~before/after C3D_RenderTargetDelete`, `FB ~before C3D_Fini`, `GM after I_ShutdownGraphics`, `GameMain returning`, `T+130166 ms: GameMain exit 0`, `after SDL_Quit`, `after gfxExit`, `atexit callback` ✅
- **Sin crash.** Vuelta al HOME Menu.

**Tiempo de cierre:** T+102341 (MAP01) a T+130166 (GameMain exit 0) = ~27 825 ms. El cierre en sí es rápido (todas las `[EXIT]` en línea 86 sin timestamps intermedios), pero el tiempo total incluye el suspend de APT y la transición.

**Tabla de hipótesis:**

| Hipótesis | Resultado |
|---|---|
| **A (romfsExit es la causa):** sin `romfsExit()`, el devoptab de romfs: permanece registrado. Newlib cierra FILE* sin fallar. Consola vuelve a HOME sin crash. | ✅ **CONFIRMADA** |
| **B (romfsExit no es la causa):** el crash se produce por otra razón. Pantalla de error de Luma. | ❌ **DESCARTADA** |

**Conclusión (tres columnas):**

| Demostrado | Probable | Sin datos |
|---|---|---|
| Quitar `romfsExit()` de `main()` evita el crash post-main. Cierre completo: `D_Cleanup()`, post-cleanup, `SDL_Quit`, `gfxExit`, `atexit` y vuelta al HOME Menu sin crash. `romfsExit()` desregistraba el devoptab de "romfs:" mientras Newlib aún tenía FILE* abiertos a archivos .pk3 de romfs:, causando `_lseek_r` con FAR=0x18 (puntero nulo + offset de `seek_r`). | El cierre lento (~28 s desde MAP01 hasta GameMain exit) incluye el tiempo de suspend de APT y la transición del sistema al HOME Menu. La música del HOME se corta porque el sistema libera recursos de audio al suspender la aplicación y los recupera al volver. | Tiempo exacto desde `gameloop_abort set` hasta la última marca (sin timestamp intermedio); si el cierre lento es del juego o del sistema APT; por qué la música del HOME se corta (sin logs del sistema) |
