#!/bin/sh
# Requiere: g++, SDL2 y SDL2_image (Debian/Ubuntu: sudo apt install g++ libsdl2-dev libsdl2-image-dev libnotify-bin)
#                                   (macOS: brew install sdl2 sdl2_image)
g++ -std=c++17 -O2 -pthread agenda_pipboy.cpp -o agenda_pipboy $(sdl2-config --cflags --libs) -lSDL2_image && echo "Listo: ./agenda_pipboy"
