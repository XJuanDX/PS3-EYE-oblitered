# PS3 EYE Oblitered — Driver y Cámara Virtual para Windows 10 y 11

Driver nativo, cámara virtual DirectShow y panel de control en español para usar la cámara **Sony PlayStation Eye** (con **video y micrófono integrados**) en cualquier computadora con Windows 10 u 11 de 64 bits.

Funciona directamente en **Google Chrome, Microsoft Edge, Discord, OBS Studio, Zoom, Google Meet, Teams, WhatsApp y VLC** sin necesidad de controladores de pago antiguos (como CL-Eye) ni configuraciones complejas con Zadig.

---

## Características Principales

- **Plug & Play Real:** Conectas la cámara, ejecutas el instalador y ya funciona.
- **Audio y Video Completos:** Video en tiempo real y micrófono de 4 canales a 16 kHz funcionando a la vez.
- **Compatible con Secure Boot:** 100% en modo usuario, sin riesgos de pantalla azul (BSOD) ni necesidad de desactivar la seguridad de Windows.
- **Cero dependencias:** No necesitas instalar redistribuibles de Visual C++ ni programas adicionales.
- **Panel de Control en Español:** Ajusta brillo, contraste, exposición, ganancia, balance de blancos y volteo de imagen desde la bandeja del sistema (junto al reloj).
- **Multi-aplicación:** Puedes usar la cámara en Discord y OBS al mismo tiempo sin que se congele.

---

## Guía Rápida de Instalación (Paso a Paso)

### Paso 1: Conecta la cámara
Enchufa tu cámara PlayStation Eye a un puerto USB de tu computadora (preferiblemente un puerto trasero directo en la placa madre si usas PC de escritorio).

### Paso 2: Ejecuta el instalador
1. En esta carpeta, busca el archivo llamado **`install.bat`**.
2. Haz **doble clic** sobre él.
3. Si Windows te pregunta *"¿Quieres permitir que esta aplicación haga cambios en el dispositivo?"*, haz clic en **Sí** (necesita permisos para registrar el controlador en el sistema).
4. Se abrirá una ventana de fondo negro que completará 4 pasos automáticamente:
   - Registro del certificado de seguridad de confianza.
   - Limpieza de drivers conflictivos anteriores (si alguna vez usaste Zadig o CL-Eye).
   - Instalación del controlador nativo de video.
   - Registro de la cámara virtual DirectShow y puesta en marcha en segundo plano.
5. Cuando veas el mensaje verde **`[EXITO] Controlador y Camara Virtual Instalados!`**, presiona cualquier tecla para cerrar la ventana.

---

## Cómo usarla en tus aplicaciones

Abre tu programa favorito y selecciona los dispositivos correspondientes en sus opciones de configuración:

| Ajuste | Nombre que debes seleccionar |
|---|---|
| **Cámara / Video** | **PS3 Eye Camera** *(o PS3 Eye Universal)* |
| **Micrófono / Audio** | **Micrófono (USB Camera-B4.09.24.1)** |

### Ejemplos en aplicaciones comunes:
- **Discord:** Ajustes de usuario ➔ *Voz y Video* ➔ En *Dispositivo de entrada* selecciona el micrófono de la cámara, y abajo en *Ajustes de video* selecciona **PS3 Eye Camera**.
- **Google Chrome / Meet / Zoom:** Al entrar a una llamada o reunión, pulsa en el candado o en los ajustes de video y selecciona **PS3 Eye Camera**.
- **OBS Studio:** En tu escena, agrega una nueva fuente de *Dispositivo de captura de video* y selecciona **PS3 Eye Camera**.

---

## Panel de Control y Ajustes de Imagen

Junto al reloj de Windows (en la bandeja del sistema) verás el icono de **PS3 EYE Oblitered**:

1. Haz doble clic en el icono para abrir el **Panel de Control**.
2. Desde la ventana puedes:
   - **Ver la imagen en vivo** de la cámara.
   - **Cambiar la resolución y fluidez:**
     - 640x480 a 30 FPS *(Recomendado para videollamadas)*.
     - 640x480 a 60 FPS *(Mayor fluidez)*.
     - 320x240 a 60 FPS o 125 FPS *(Ultra alta velocidad)*.
   - **Ajustar la imagen:** Ganancia, Exposición, Brillo, Contraste, Nitidez, Auto-exposición y Balance de blancos.
   - **Efectos:** Espejo horizontal (para verte como en un espejo) y Anti-parpadeo de luz (50Hz/60Hz).
   - **Iniciar con Windows:** Marca esta casilla si deseas que la cámara esté siempre lista cada vez que enciendas tu PC.
3. Para ocultar la ventana sin cerrar la cámara, presiona **Minimizar a la bandeja**. Estando minimizada, el consumo de tu procesador (CPU) es inferior al 0.2%.

---

## Preguntas Frecuentes y Solución de Problemas

#### ¿Por qué el micrófono no se escuchaba antes de este instalador?
Si alguna vez utilizaste herramientas como Zadig para instalar WinUSB en el dispositivo completo, se eliminaba la división de audio de Windows. Nuestro instalador `install.bat` detecta automáticamente esa situación y restaura el driver de audio oficial de Microsoft (`usbaudio.sys`) sin que tengas que hacer nada.

#### La imagen se ve oscura o con mucho grano
Abre el panel de control desde la bandeja del sistema y activa las casillas **Auto Ganancia** y **Auto Exposición**, o sube manualmente la barra de **Exposición** y **Brillo**.

#### La cámara no enciende o dice "Buscando cámara..."
1. Desconecta el cable USB de la cámara y vuelve a conectarlo en otro puerto USB.
2. Evita conectar la cámara a multiplicadores (hubs) USB sin alimentación propia.

---

## Cómo Desinstalar

Si alguna vez deseas quitar completamente el controlador y la cámara virtual de tu equipo:
1. Haz doble clic en el archivo **`uninstall.bat`**.
2. Acepta los permisos de Administrador.
3. El asistente cerrará los procesos, desregistrará la cámara virtual y removerá los certificados del sistema de forma limpia.

---

## Estructura del Proyecto (Avanzado)

Para desarrolladores o usuarios curiosos:
- `bin/`: Contiene los binarios precompilados vinculados estáticamente (`/MT`): `ps3eye_dshow.dll` y `PS3EYEOblitered.exe`.
- `drivers/`: Archivo de instalación PnP `ps3eye_video.inf` y catálogo firmado digitalmente `ps3eye_video.cat`.
- `src/`: Código fuente en C/C++ del filtro DirectShow COM, el protocolo IPC de memoria compartida lock-free y la interfaz nativa Win32.
- `build.ps1`: Script PowerShell de compilación para MSVC (Visual Studio 2019 / 2022).
