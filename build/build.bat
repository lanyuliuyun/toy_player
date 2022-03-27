@echo off

cmake .. ^
    -DLY_ROOT=F:/opensource/libyuv ^
    -DFFMPEG_ROOT=D:/ffmpeg-n4.4-178-g4b583e5425-win64-gpl-shared-4.4 ^
    -DOPUS_ROOT=F:/opensource/opus-v1.3.1

cmake --build . --config Debug;CharacterSet=Unicode
