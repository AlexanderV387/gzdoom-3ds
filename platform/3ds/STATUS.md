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

## Pregunta abierta

**¿El cuelgue lo provoca el shader del motor (vértice o fragmento), o la configuración del estado de renderizado que se comparte entre todos los dibujos?** El shader se compila desde `c3d_main_shbin.h` (shaders precompilados del motor). El triángulo mínimo usa los mismos uniformes y el mismo programa shader.

## Prueba 25 (hecha, con 4 pasadas)

**Resultado:**
- **A (ref):** `GPU HANG` en frame 1, como siempre. ✅ Referencia confirmada.
- **B (`c3d-nodraw.txt`):** frames 0-11 todos `begin=ok`, sin `GPU HANG`. ✅ **H1 confirmada**: el dibujo cuelga.
- **C (`c3d-triangle.txt`):** `GPU HANG` en frame 1 con triángulo mínimo. ❌ No es solo el dibujo del motor: el shader o el estado compartido fallan.
- **D (`c3d-notex.txt`):** `GPU HANG` en frame 1 sin textura. ❌ **H3 descartada**.

**Hipótesis descartadas:** H2 (no es el dibujo), H3 (textura), H4 (buffers del motor).
**Hipótesis confirmada:** H1 (el dibujo cuelga), pero la causa es más profunda: algo común a todo dibujo, incluido el triángulo mínimo.

**Nuevo foco:** el shader del motor (vértice/fragmento) o la configuración del estado de renderizado compartido.

## Análisis de la prueba 25

La prueba 25 confirmó que el cuelgue lo provoca **cualquier** comando de dibujo enviado a la GPU, no solo el del motor. Con `c3d-nodraw.txt` los fotogramas corren sin problemas, pero con `c3d-triangle.txt` (un triángulo mínimo sin nada del motor) la GPU se cuelga igual.

Esto descarta que el problema sean los buffers de vértices del motor, las texturas o los índices. Quedan dos sospechosos:
- **El shader del motor** (`c3d_main.v.pica`): tiene 4 uniformes (mvp[4]=16 slots, colorscale, colormul, texmat[2]=2) y 3 atributos. El ejemplo `simple_tri` que funciona usa solo 1 uniforme y 2 atributos.
- **El estado de renderizado compartido**: combiners, blending, depth test, viewport, render target. Se configuran una vez y se heredan a todo dibujo.

## Prueba 26 (propuesta, sin aprobar)

**Pregunta:** ¿El shader del motor (`c3d_main.v.pica`) es el que cuelga la GPU, o es el estado de renderizado compartido?

**Hipótesis A: El shader del motor es el culpable.**
- Si usamos un shader mínimo (como `simple_tri`) para el triángulo de prueba, la GPU no se cuelga.
- En el log: `frame 0: begin=ok ... draws: arrays=1`, luego `frame 1: begin=ok`. Sin `GPU HANG`.

**Hipótesis B: No es el shader, es el estado de renderizado compartido.**
- Aunque usemos un shader mínimo, la GPU se sigue colgando.
- En el log: `GPU HANG: frame 1 could not start`, igual que siempre.

**Única variable que cambia:** el shader program bound al dibujar el triángulo. Nuevo interruptor `c3d-minimal-shader.txt`: si existe, `DrawTestTriangle()` compila y usa un shader mínimo (1 uniforme `projection[4]`, 2 atributos: posición y color) en lugar del shader del motor.

Archivos que se tocan:
- `c3d_diag.h` / `c3d_diag.cpp`: nuevo interruptor `MinimalShader`.
- `c3d_framebuffer.cpp`: `DrawTestTriangle()` conlaza un shader mínimo cuando `MinimalShader` es true.

**Pasadas en la consola:**

| Pasada | Archivos de control | Qué mirar |
|---|---|---|
| A (ref) | `c3d-triangle.txt` | Pantalla negra (hang, como siempre) |
| B | `c3d-triangle.txt` + `c3d-minimal-shader.txt` | Triángulo rojo si H_A; pantalla negra si H_B |

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

## Pendientes que no son la pregunta abierta

- **Texto en ruso:** línea 24 de los logs: `В игрока %o попала межпространственная слизь!` se pega con `Selecting OpenGL backend...` sin separador. Es un bug de formato del motor (`%o` no sustituido). No afecta a la 3DS.
- **HOME no responde:** en ninguna de las 4 pasadas el botón HOME abrió la consola de depuración. Puede ser porque el hilo de la consola no arranca o porque el cuelgue de GPU bloquea el hilo principal. Pendiente de investigar.
- Entrada de mandos: el gestor del joystick está vacío (punto 2).
- Líneas y puntos (automapa), niebla, luz, portales, sonido, controles 3DS, pantalla de abajo, `.cia` (unique ID 0xF3D5C).
- La carga de las fuentes TrueType latinas genera atlas de 1 MB cada una con el juego de caracteres completo: lento y caro en memoria. Recortar el juego de caracteres cuando el resto funcione.
