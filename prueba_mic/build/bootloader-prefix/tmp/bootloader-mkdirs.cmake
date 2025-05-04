# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Users/alexm/OneDrive/Escritorio/S-Embebidos/v5.3/esp-idf/components/bootloader/subproject"
  "C:/Users/alexm/OneDrive/Escritorio/S-Embebidos/proyectos_E/blink/main/IOT/prueb_proyecto/prueba_mic/build/bootloader"
  "C:/Users/alexm/OneDrive/Escritorio/S-Embebidos/proyectos_E/blink/main/IOT/prueb_proyecto/prueba_mic/build/bootloader-prefix"
  "C:/Users/alexm/OneDrive/Escritorio/S-Embebidos/proyectos_E/blink/main/IOT/prueb_proyecto/prueba_mic/build/bootloader-prefix/tmp"
  "C:/Users/alexm/OneDrive/Escritorio/S-Embebidos/proyectos_E/blink/main/IOT/prueb_proyecto/prueba_mic/build/bootloader-prefix/src/bootloader-stamp"
  "C:/Users/alexm/OneDrive/Escritorio/S-Embebidos/proyectos_E/blink/main/IOT/prueb_proyecto/prueba_mic/build/bootloader-prefix/src"
  "C:/Users/alexm/OneDrive/Escritorio/S-Embebidos/proyectos_E/blink/main/IOT/prueb_proyecto/prueba_mic/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/alexm/OneDrive/Escritorio/S-Embebidos/proyectos_E/blink/main/IOT/prueb_proyecto/prueba_mic/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/alexm/OneDrive/Escritorio/S-Embebidos/proyectos_E/blink/main/IOT/prueb_proyecto/prueba_mic/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
