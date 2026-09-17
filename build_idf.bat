@echo off
set IDF_PATH=C:\esp\v5.5.3\esp-idf
set IDF_TOOLS_PATH=C:\Espressif\tools
set IDF_PYTHON_ENV_PATH=C:\Espressif\tools\python\v5.5.3\venv
set ESP_ROM_ELF_DIR=C:\Espressif\tools\esp-rom-elfs\20241011\
set PATH=C:\Espressif\tools\python\v5.5.3\venv\Scripts;C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20251107\xtensa-esp-elf\bin;C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;%PATH%
cd /d C:\Users\Bethelss\sample_project
C:\Espressif\tools\python\v5.5.3\venv\Scripts\python.exe C:\esp\v5.5.3\esp-idf\tools\idf.py %*
