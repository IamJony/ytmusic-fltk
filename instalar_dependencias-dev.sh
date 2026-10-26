#!/bin/sh
set -e

echo "=== Instalando herramientas y dependencias de compilación ==="

# 1. Instalar el compilador GCC y sus librerías base
tce-load -wi gcc gcc_libs

# 2. Instalar el entorno esencial de desarrollo (make, glibc-dev, etc.)
tce-load -wi compiletc

# 3. Instalar las cabeceras de desarrollo de la interfaz gráfica
tce-load -wi fltk-1.3-dev curl-dev

echo "=== ¡Entorno de desarrollo y GCC listos para compilar! ==="
