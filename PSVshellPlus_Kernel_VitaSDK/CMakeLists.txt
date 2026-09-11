cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "VitaSDK toolchain")
  else()
    message(FATAL_ERROR "VITASDK is not set")
  endif()
endif()

project(PSVshellPlus_Kernel C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2 -std=gnu11 -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -nostdlib -Wl,--gc-sections")

add_executable(${PROJECT_NAME}
  src/main.c
  src/probe.c
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME}
  gcc
  taihenForKernel_stub
  taihenModuleUtils_stub
  SceThreadmgrForDriver_stub
  SceSysclibForDriver_stub
  SceSysmemForDriver_stub
  ScePowerForDriver_stub
  SceIofilemgrForDriver_stub
)

vita_create_self(${PROJECT_NAME}.skprx ${PROJECT_NAME}
  UNSAFE
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/PSVshellPlus_Kernel.yml
)
