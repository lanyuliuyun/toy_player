@echo off

cmake -G "Visual Studio 15 2017 Win64" ..

cmake --build . --config Debug;CharacterSet=Unicode
