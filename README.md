# TEMPer Monitor 1.2

**Español** | [English](#english)

Aplicación de escritorio para Windows que lee un sensor USB TEMPer compatible mediante HID, muestra la temperatura y actualiza un archivo TXT para integrarlo con otras aplicaciones.

## Funciones

- Lectura del sensor USB y reconexión automática.
- Visualización en grados Celsius o Fahrenheit.
- Calibración ajustable de −10,0 °C a +10,0 °C en pasos de 0,5 °C.
- Intervalos de lectura configurables: 5 a 60 segundos en pasos de 5 segundos, o 2 a 60 minutos.
- Interfaz en español, inglés, portugués, francés, italiano y alemán.
- Archivo TXT con un único valor, actualizado en cada lectura correcta (ejemplo: `22,3°C`).
- Configuración persistente por usuario.

## Compatibilidad y requisitos

- Windows; compilación con C++17, CMake 3.21 o superior y Qt 6.8 Widgets.
- Compilador MSVC compatible y SDK de Windows con bibliotecas SetupAPI e HID.
- El dispositivo comprobado durante el desarrollo es **TEMPer1V1.2, VID 0C45 / PID 7401, interfaz MI_01**. No se garantiza compatibilidad con otros modelos.

## Compilar

1. Instalar Qt 6.8 (módulo Widgets), CMake y Visual Studio Build Tools con soporte C++ para Windows.
2. Abrir una consola de compilación MSVC x64 con Qt disponible para CMake.
3. Ejecutar:

   ```powershell
   cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.8.2/msvc2022_64"
   cmake --build build --config Release
   ```

   Adaptar la ruta de Qt a la instalación local. El ejecutable requiere las bibliotecas de Qt correspondientes; para distribuirlo se puede utilizar `windeployqt` y cumplir las condiciones de licencia de Qt.

## Archivo TXT

La aplicación escribe un único valor de temperatura, sin salto de línea ni BOM, con coma decimal y símbolo de unidad. Se utiliza codificación Latin-1 compatible con Windows-1252. La ruta de salida se configura desde la interfaz. **Comprobar la ruta antes de utilizarla con otros sistemas**, porque el archivo existente se reemplaza con cada lectura correcta.

## Configuraciones anteriores

La versión 1.2 guarda sus preferencias de forma independiente mediante la configuración propia de TEMPer Monitor. Las preferencias de idioma, unidad de temperatura, intervalo de lectura y demás ajustes se conservan entre ejecuciones, sin depender de instalaciones anteriores.

## Autoría

Desarrollado por **Carlos Grandi** con la colaboración de **TARS (ChatGPT de OpenAI)**.

## Licencia

Código fuente publicado bajo licencia MIT; ver [LICENSE](LICENSE). Qt y cualquier componente externo conservan sus propias licencias.

---

## English

TEMPer Monitor 1.2 is a Windows desktop application that reads a compatible TEMPer USB HID temperature sensor, displays the measurement, and updates a plain-text file for integration with other software.

### Features

- USB HID reading and automatic reconnection.
- Celsius and Fahrenheit display.
- Temperature offset from −10.0 °C to +10.0 °C in 0.5 °C steps.
- Configurable sampling intervals: 5–60 seconds in 5-second steps, or 2–60 minutes.
- Six interface languages: Spanish, English, Portuguese, French, Italian and German.
- One-value TXT output, updated on each successful reading (example: `22,3°C`).
- Persistent per-user settings.

### Requirements and build

Windows, C++17, CMake 3.21+, Qt 6.8 Widgets, a compatible MSVC toolchain and Windows SDK. The hardware verified during development was **TEMPer1V1.2 (VID 0C45, PID 7401, MI_01)**; other devices are not guaranteed to work.

Configure and build with:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.8.2/msvc2022_64"
cmake --build build --config Release
```

Adjust the Qt path as needed. Use `windeployqt` when preparing a Windows distribution and comply with applicable Qt licensing terms.

The TXT file contains one temperature value without a newline or BOM, using a comma decimal separator and Latin-1/Windows-1252-compatible encoding. The selected output file is replaced on each successful reading. Version 1.2 stores its settings independently and preserves user preferences between sessions without relying on previous installations.

### Credits

Developed by **Carlos Grandi** with **TARS (ChatGPT by OpenAI)**.

Source code: MIT License. See [LICENSE](LICENSE). Third-party components have separate licenses.
