@echo off
cd /d "%~dp0"
python studio\build.py --dx11 --exe MoleculeStudio.exe %*
exit /b %errorlevel%
