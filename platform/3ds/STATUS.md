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
11. **El shader del motor es el culpable.** Con el shader mínimo de `simple_tri` (un uniforme, dos atributos, sin texcoord output) el triángulo se dibuja correctamente (prueba 26, log A). Con el shader del motor (8 uniformes, 3 atributos, texcoord output) la GPU se cuelga (prueba 26, log B). El render target y la GPU en general funcionan.

## Pregunta abierta

**¿Qué aspecto del shader del motor causa el cuelgue?** ¿Son los **uniformes extra** (`colorscale`, `colormul`, `texmat[2]` que suman 4 fvecs más), las **instrucciones extra** (dp4 para texcoords y mul para color), o el **output de texcoord** (`outtc0`)?

El shader del motor (`c3d_main.v.pica`) vs el de `simple_tri`:
- Uniformes: 8 fvecs (mvp[4]+colorscale+colormul+texmat[2]) vs 4 fvecs (projection[4])
- Atributos: 3 (v0=v1=v2) vs 2 (v0=v1)
- Outputs: 3 (outpos+outtc0+outcol) vs 2 (outpos+outclr)
- Instrucciones: 11 (dp4×6, mov×3, mul×2) vs 7 (dp4×4, mov×3)

## Prueba 26 (hecha, con 2 pasadas)

**Resultado:**
- **A** (`c3d-triangle.txt` + `c3d-minimal-shader.txt`): `minshader=1`, frames 0-11 `begin=ok`, sin `GPU HANG`. Triángulo visible en pantalla. ✅ **El shader de simple_tri funciona.**
- **B** (`c3d-triangle.txt` solo): `minshader=0`, `GPU HANG` en frame 1. ❌ **El shader del motor cuelga.**

**Conclusión:** El problema es el shader del motor (`c3d_main.v.pica`) o cómo se configuran sus uniformes. El render target, la GPU y citro3d están sanos.

## Prueba 27 (propuesta, sin aprobar)

**Pregunta:** ¿El cuelgue lo provocan los uniformes extra del shader del motor, o las instrucciones extra?

**Diseño:** shader intermedio (`c3d_mid.v.pica`) que tiene los mismos uniformes que el shader del motor (8 fvecs) pero solo las instrucciones de simple_tri (dp4×4 para posición + mov para color). Sin texcoord output, sin dp4 extra, sin mul.

**Hipótesis A: Son los uniformes extra.**
- El shader intermedio se cuelga igual que el del motor.
- En el log: `GPU HANG` en frame 1.

**Hipótesis B: Son las instrucciones extra (dp4 para texcoords, mul para color).**
- El shader intermedio funciona como simple_tri.
- En el log: frames 0-11 `begin=ok`, sin `GPU HANG`.

**Hipótesis C: Es el output de texcoord (`outtc0`).**
- Mismo resultado que B (porque el intermedio no tiene `outtc0`).
- Para distinguir B de C se necesitaría un test 28.

**Única variable que cambia:** el shader program. Todo lo demás igual que test 26 (mismos vértices, mismo render target, mismos combiners).

Nuevo interruptor `c3d-midshader.txt`: si existe junto a `c3d-triangle.txt`, usa `c3d_mid.v.pica` en vez del shader del motor.

**El shader intermedio (`c3d_mid.v.pica`):**
```
.fvec mvp[4]
.fvec colorscale
.fvec colormul
.fvec texmat[2]

.constf ones(1.0, 1.0, 1.0, 1.0)

.out outpos position
.out outcol color

.alias inpos v0
.alias incol v1

.proc main
    mov r0.xyz, inpos
    mov r0.w,   ones
    dp4 outpos.x, mvp[0], r0
    dp4 outpos.y, mvp[1], r0
    dp4 outpos.z, mvp[2], r0
    dp4 outpos.w, mvp[3], r0
    mov outcol, incol
    end
.end
```

Mismos 8 uniformes que el motor, pero solo 7 instrucciones como simple_tri, y sin `outtc0`.

**Pasadas en la consola:**

| Pasada | Archivos de control | Qué mirar |
|---|---|---|
| A (ref minimal) | `c3d-triangle.txt` + `c3d-minimal-shader.txt` | Triángulo visible (confirmar que funciona) |
| B (intermedio) | `c3d-triangle.txt` + `c3d-midshader.txt` | Triángulo visible si H_B/C; pantalla negra si H_A |

Archivos que se tocan:
- `src/common/rendering/c3d/c3d_mid.v.pica`: nuevo shader intermedio.
- `src/CMakeLists.txt`: añadir picasso + bin2s para el shader intermedio.
- `c3d_diag.h` / `c3d_diag.cpp`: nuevo interruptor `MidShader`.
- `c3d_framebuffer.cpp`: camino para el shader intermedio en `DrawTestTriangle()`.

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

## Pendientes que no son la pregunta abierta

- **Texto en ruso:** línea 24 de los logs: `В игрока %o попала межпространственная слизь!` se pega con `Selecting OpenGL backend...` sin separador. Causa desconocida.
- **HOME no responde:** en ninguna de las pasadas el botón HOME volvió al menú de la 3DS. En la pasada A del test 26 los fotogramas avanzaban sin cuelgue y tampoco respondió, así que no es por el cuelgue de GPU. Pendiente de investigar.
- Entrada de mandos: el gestor del joystick está vacío (punto 2).
- Líneas y puntos (automapa), niebla, luz, portales, sonido, controles 3DS, pantalla de abajo, `.cia` (unique ID 0xF3D5C).
- La carga de las fuentes TrueType latinas genera atlas de 1 MB cada una con el juego de caracteres completo: lento y caro en memoria. Recortar el juego de caracteres cuando el resto funcione.
