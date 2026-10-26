#!/bin/sh
set -e

echo "=== Instalando dependencias de ejecución para ytmusic-fltk ==="

# Instala los paquetes .tcz necesarios para el funcionamiento de la app
tce-load -wi curl fltk-1.3-dev mplayer-cli ffmpeg4 libwebp1 gcc_libs yt-dlp libEGL

echo "=== ¡Instalación completada con éxito! ==="
