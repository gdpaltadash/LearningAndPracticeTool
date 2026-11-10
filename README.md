# Macro Practice (Geode, GD 2.2081)

Mod de práctica para aprenderse niveles a partir de una macro. UI en inglés, hecha con nodos de Cocos desde cero.

Formato de macro soportado: **GDR2** (`.gdr2` / `.gdr`), el que usa zBot y otros bots que usan la librería GDR.

## Cómo compilarlo en GitHub

1. Cambia `id` y `developer` en `mod.json` (ahora dicen `yourname`), por ejemplo `tuusuario.macro-practice`.
2. Crea un repositorio en GitHub y sube **todo el contenido de esta carpeta** (incluida la carpeta oculta `.github`).
3. Ve a la pestaña **Actions**. El workflow *Build Mod* corre solo en cada push, o lo lanzas a mano con *Run workflow*.
4. Cuando termine, descarga el artefacto **Macro Practice (.geode)** y mete el `.geode` en la carpeta `geode/mods` de tu GD (o ábrelo con Geode).

Usa Geode **v5.11.2** (la última para GD 2.2081) y la librería [GDReplayFormat](https://github.com/maxnut/GDReplayFormat) fijada al commit `d8d2f0b`.

## Cómo se usa

Pausa el nivel y pulsa **Macro Practice** (abajo a la derecha).

- **Load Macro**: importa la macro. Se recuerda por nivel y se vuelve a cargar sola la próxima vez.
- **Mode**
  - **Early Fix**: juegas normal. Los clicks/releases que des *antes* (hasta `Max early frames`) se retrasan al frame exacto de la macro. Los tardíos se dejan tal cual. Te dice cuánto te adelantaste (`Fixed 3f early`).
  - **Watch**: la macro juega el nivel con click sounds. Tus inputs se ignoran.
  - **Trace**: la macro juega el nivel, tus clicks solo se miden. Precisión en vivo y reporte final (perfect / early / late / missed / extra, offset medio en frames y ms).
- **Restart Level**: reinicia desde el principio con el modo ya activo (Watch y Trace lo necesitan).
- **Last Results**: reporte del intento actual o del último terminado.

Ajustes: `Max early frames`, `Scoring window` (ventana de emparejado y de puntuación), `Macro frame offset`, `Game TPS` (Auto = el de la macro), volúmenes, y toggles del HUD.

**Indicadores**
- Carril abajo en pantalla: círculos = presionar, rombos = soltar (verde/rojo P1, azul/morado P2). La línea blanca es "ahora". Tus clicks aparecen como marcas: dorado = perfecto, azul = temprano, naranja = tarde.
- Puntos dentro del nivel: aparecen donde la macro hizo cada input. Se graban cuando la macro juega (Watch/Trace), así que hay que dar **una pasada en Watch o Trace** antes de que salgan en Early Fix.

**Stats**: con cualquier modo activo no se guarda nada (progreso, attempts, stats, completions). Al llegar al final del nivel sale el reporte y el juego se pausa.

## Si algo no cuadra

- **En Watch la macro muere o se desfasa**: prueba `Macro frame offset` en `-1` o `+1`. Cada bot cuenta el frame con una convención algo distinta; el mod usa la de zBot (`m_currentProgress - 1`).
- **Macro grabada con otro TPS** (por ejemplo 360 con physics bypass): pon `Game TPS` igual al TPS al que corre tu juego. En `Auto` se asume que son iguales.
- Si el archivo no es GDR2, el mod lo dice. Otros formatos (por ejemplo `.zbf`, `.mhr`) no están soportados todavía.

## Estructura

```
src/core/Macro.*      lector GDR2 (C++ puro)
src/core/Engine.hpp   corrección de early inputs, emparejado y puntuación (C++ puro, probado)
src/Session.*         estado global, ajustes, sonidos, bloqueo de stats
src/hooks.cpp         hooks de Geode (inputs, nivel, stats, menú de pausa)
src/ui/               HUD, panel de configuración, popup de resultados
tests/test_core.cpp   tests de la lógica (se compilan aparte, sin Geode)
```

Tests de la lógica (necesitan g++ 13+ y los headers de GDReplayFormat):

```
g++ -std=c++23 -Isrc/core -I<ruta a GDReplayFormat>/include tests/test_core.cpp src/core/Macro.cpp -o test_core && ./test_core
```
