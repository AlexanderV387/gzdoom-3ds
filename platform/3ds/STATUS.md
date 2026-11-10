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

## Pregunta abierta

¿Por qué el output de texcoord (`outtc0`) cuelga la GPU? ¿Es que el hardware de la Pica espera que si hay un output de texcoord haya una textura bindada en el tex env? ¿O es que las instrucciones `dp4` que escriben `outtc0` leen datos de `texmat` que no están bien alineados o no se han escrito correctamente? La prueba 28 lo aclarará.

Uniformes del shader del motor (valores del log A, prueba 26): `mvp=0`, `colorscale=4`, `colormul=5`, `texmat=6`. DVLB con 1 DVLE.

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

**Lo que ya sabemos:**
- Test 26, log A: `simple_tri` shader funciona (triángulo visible, 12 frames sin hang).
- Test 26, log B: shader del motor (`c3d_main.v.pica`) con datos del motor cuelga.
- Pero test 26 cambió shader, matriz, vértices y atributos a la vez.

**5 variantes en una sola compilación.** Cada una se activa con un archivo vacío en la SD:

| Var | Shader | Instrucciones | Uniformes | Color en | `outtc0` | ¿Qué prueba? |
|---|---|---|---|---|---|---|
| V0 | `c3d_main` (motor) | 11 (full) | 8 fvecs | v2 | sí | Baseline: esperado HANG |
| V1 | `c3d_nouttc0` | 9 (-2 dp4 tc) | 8 fvecs | v2 | **no** | ¿Es `outtc0`? |
| V2 | `c3d_nomul` | 9 (-mul +mov) | 8 fvecs | v2 | sí | ¿Es el `mul` del color? |
| V3 | `c3d_intermed` | 7 (=simple_tri) | 8 fvecs | v2 | **no** | ¿Son los 8 uniformes? |
| V4 | `simple_tri` | 7 | 4 fvecs | v1 | no | ¿Son los datos del motor? |

**Diferencias entre variantes (una sola variable cada paso):**

| Cambio | V0→V1 | V1→V2 | V2→V3 | V3→V4 |
|---|---|---|---|---|
| Shader | nouttc0 (-outtc0, -2dp4) | nomul (-mul,+mov) | intermed (-tex dp4) | simple_tri (4 uniforms) |
| outtc0 | ❌→❌ | ❌→✅ | ✅→❌ | ❌→❌ |
| mul color | ✅→✅ | ✅→❌ | ❌→❌ | ❌→❌ |
| Uniformes | 8→8 | 8→8 | 8→8 | 8→4 |
| Color attr | v2→v2 | v2→v2 | v2→v2 | v2→v1 |

**Hipótesis:**

**Autocomprobación (plantilla-prueba.md):**
1. ¿Una sola pregunta? Sí: ¿qué aspecto del shader/datos causa el cuelgue?
2. ¿Resultados distinguibles en el log? Sí: `GPU HANG` vs frames 0-11 `begin=ok`.
3. ¿Una sola variable por paso? Sí (tabla de diferencias arriba).
4. ¿Hipótesis con resultados distintos? Sí (H1-H4).
5. ¿El código no rompe el motor en PC? Sí, todo detrás de `#ifdef __3DS__` / diag switch.
6. ¿La línea `Test:` del log dirá 27? Sí, `c3d_diag.cpp` la escribe con el número de prueba.
7. ¿Los marcadores se quitan cuando la pregunta está respondida? Sí, al actualizar STATUS.md.

**Interruptores:** archivos vacíos `c3d-v0.txt` a `c3d-v4.txt` en `sdmc:/3ds/gzdoom/`, junto con `c3d-triangle.txt`.

**Pasadas en la consola:**

| Pasada | Archivos de control en sdmc:/3ds/gzdoom/ | Qué mirar en pantalla |
|---|---|---|
| V0 | `c3d-triangle.txt` + `c3d-v0.txt` | Pantalla negra (HANG esperado) |
| V1 | `c3d-triangle.txt` + `c3d-v1.txt` | Triángulo si funciona, negra si HANG |
| V2 | `c3d-triangle.txt` + `c3d-v2.txt` | Triángulo si funciona, negra si HANG |
| V3 | `c3d-triangle.txt` + `c3d-v3.txt` | Triángulo si funciona, negra si HANG |
| V4 | `c3d-triangle.txt` + `c3d-v4.txt` | Triángulo si funciona, negra si HANG |

Esperar al menos 60 s en cada pasada para que se escriban los 12 frames del log.

Archivos nuevos:
- `src/common/rendering/c3d/c3d_nouttc0.v.pica`: shader sin `outtc0` ni sus 2 dp4.
- `src/common/rendering/c3d/c3d_nomul.v.pica`: shader con `mov outcol, incol` en vez de `mul`.
- `src/common/rendering/c3d/c3d_intermed.v.pica`: shader con 8 uniformes pero cuerpo de simple_tri.
- `src/CMakeLists.txt`: reglas picasso + bin2s para los 3 nuevos shaders.
- `c3d_diag.h` / `c3d_diag.cpp`: `Test27Variant` (-1 a 4).
- `c3d_framebuffer.cpp`: `DrawTestTriangle()` con ramas para V0-V4.

## Prueba 28 (propuesta, sin compilar)

**Pregunta:** ¿El cuelgue es por tener `outtc0` declarado como output del shader, o por escribirle valores calculados con `dp4`?

**Lo que ya sabemos (con la prueba que lo mostró):**
- Test 27: sin `outtc0` (V1, V3) → OK. Con `outtc0` (V0, V2) → HANG.
- Los datos del motor no son el problema (V4).
- No son los 8 uniformes (V3).
- No es el `mul` (V2).

**Hipótesis A: Es el output `outtc0` en sí.** La Pica se cuelga cuando un shader declara `.out outtc0 texcoord0` pero no hay textura bindada en el tex env. Aunque no se escriba nada útil, el hecho de que el output exista es suficiente.
- Si A es cierta: V0 (outtc0 = constante 0,0) se cuelga igual que V1 (dp4).

**Hipótesis B: Son las instrucciones `dp4` que escriben `outtc0`.** Escriben valores calculados desde `texmat`, y algo en esa operación es lo que cuelga. Si se escribe un valor constante, funciona.
- Si B es cierta: V0 (outtc0 = constante 0,0) funciona, V1 (dp4) se cuelga.

**Hipótesis C: Es el par `outtc0` + tex env sin textura.** El shader declara `outtc0` y el tex env está configurado para usar texcoords (como en el motor real). Sin textura bindada, la Pica se cuelga. Si se binda una textura 1x1 de relleno, funciona.
- Si C es cierta: V0 y V1 se cuelgan igual (ambos tienen outtc0 + tex env sin textura). Pero V2 (mismo shader + textura 1x1) funciona.

**2 variantes (+ baseline):**

| Var | Shader | `outtc0` | Textura bindada | ¿Qué prueba? |
|---|---|---|---|---|
| V0 | motor + `mov outtc0, (0,0,0,0)` | sí (constante) | no | ¿Es dp4 o es outtc0 en sí? |
| V1 | motor (full, dp4) | sí (dp4) | no | Baseline: esperado HANG |
| V2 | motor (full, dp4) | sí (dp4) | **sí** (1x1 blanca) | ¿Falta textura? |

**Única variable que cambia:** En V0 solo cambia el cálculo de `outtc0` (dp4 → constante). En V2 solo cambia si hay textura bindada.

**Pasadas en la consola:**

| Pasada | Archivos de control | Qué mirar en pantalla |
|---|---|---|
| V0 | `c3d-triangle.txt` + `c3d-v0.txt` | Triángulo si funciona, negra si HANG |
| V1 | `c3d-triangle.txt` + `c3d-v1.txt` | Negra (HANG baseline) |
| V2 | `c3d-triangle.txt` + `c3d-v2.txt` | Triángulo si la textura arregla, negra si no |

**Marcadores que se quedan:** los de `c3d_diag` para test 28. **Marcadores que se quitan:** los de test 27 (ya respondida).

**Cómo se compila:** `platform/3ds/docker-build-test.sh 28`.

**Autocomprobación:**
1. ¿Una sola pregunta? Sí: ¿por qué `outtc0` cuelga?
2. ¿Resultados distinguibles? Sí: HANG (70 líneas) vs OK (frames 0-11 ok).
3. ¿Una sola variable por paso? V0 vs V1: solo el cálculo de outtc0. V2 vs V1: solo la textura bindada.
4. ¿Hipótesis con resultados distintos? A: V0 HANG, V2 HANG. B: V0 OK, V2 HANG. C: V0 HANG, V2 OK.
5. ¿El código no rompe el motor en PC? Sí, detrás de `#ifdef __3DS__` / diag switch.
6. ¿La línea `Test:` dirá 28? Sí.
7. ¿Puede Alex preparar cada pasada sin equivocarse? Sí, archivos vacíos con nombres distintos.

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

## Pendientes que no son la pregunta abierta

- **Texto en ruso:** línea 24 de los logs: `В игрока %o попала межпространственная слизь!` se pega con `Selecting OpenGL backend...` sin separador. Causa desconocida.
- **HOME no responde:** en ninguna de las pasadas el botón HOME volvió al menú de la 3DS. En la pasada A del test 26 los fotogramas avanzaban sin cuelgue y tampoco respondió, así que no es por el cuelgue de GPU. Pendiente de investigar.
- Entrada de mandos: el gestor del joystick está vacío (punto 2).
- Líneas y puntos (automapa), niebla, luz, portales, sonido, controles 3DS, pantalla de abajo, `.cia` (unique ID 0xF3D5C).
- La carga de las fuentes TrueType latinas genera atlas de 1 MB cada una con el juego de caracteres completo: lento y caro en memoria. Recortar el juego de caracteres cuando el resto funcione.
