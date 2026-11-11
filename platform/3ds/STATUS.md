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

## Pregunta abierta

El shader ya está arreglado (prueba 29). La pregunta abierta es: **¿dónde se van los ~2 minutos de carga antes de que aparezca la pantalla de título?** La carga de fuentes, texturas, actores, o la primera vez que el motor llena buffers de vértices podría ser el cuello de botella. La prueba 30 aplicará el arreglo permanente y añadirá marcas de tiempo al log para medir cada fase.

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
| 30 | ¿El arreglo permanente (`mov outtc0.zw, ones` en c3d_main.v.pica) hace que el juego arranque sin archivos de control, y dónde está el cuello de botella de ~2 min? | Pendiente. |

## Pendientes que no son la pregunta abierta

- **Texto en ruso:** línea 24 de los logs: `В игрока %o попала межпространственная слизь!` se pega con `Selecting OpenGL backend...` sin separador. Causa desconocida.
- **HOME no responde:** en ninguna de las pasadas el botón HOME volvió al menú de la 3DS. En la pasada A del test 26 los fotogramas avanzaban sin cuelgue y tampoco respondió, así que no es por el cuelgue de GPU. Pendiente de investigar.
- Entrada de mandos: el gestor del joystick está vacío (punto 2).
- Líneas y puntos (automapa), niebla, luz, portales, sonido, controles 3DS, pantalla de abajo, `.cia` (unique ID 0xF3D5C).
- La carga de las fuentes TrueType latinas genera atlas de 1 MB cada una con el juego de caracteres completo: lento y caro en memoria. Recortar el juego de caracteres cuando el resto funcione.
