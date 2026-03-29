@echo off
echo Cleaning build artifacts...

rmdir /s /q x64 2>nul
rmdir /s /q Server\x64 2>nul
rmdir /s /q Client\x64 2>nul
rmdir /s /q Debug 2>nul
rmdir /s /q Release 2>nul
rmdir /s /q Server\Debug 2>nul
rmdir /s /q Server\Release 2>nul
rmdir /s /q Client\Debug 2>nul
rmdir /s /q Client\Release 2>nul
rmdir /s /q .vs 2>nul

echo Done.
