@echo off
cmake --build out --config Debug
if errorlevel 1 pause
start "" ".\out\Debug\LifeAfterLife.exe"