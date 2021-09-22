cmake_minimum_required(VERSION 3.0)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(PSP2-HTTP-Server)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "PSP2-HTTP-Server")
set(VITA_TITLEID  "PSWS00001")
set(VITA_VERSION  "01.00")
set(SELF_NAME     "http_server")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -O3 -fno-inline -fno-builtin")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -fno-exceptions")

enable_language(C ASM)

add_subdirectory(libpaf)

add_executable(${PROJECT_NAME}
  src/main.c
  src/graphics.c
  src/font.c
)

target_link_libraries(${PROJECT_NAME}
  SceHttp_stub
  SceNet_stub
  SceNetCtl_stub
  SceSsl_stub

  SceCtrl_stub
  SceDisplay_stub

  ScePaf_stub_weak
  SceSysmodule_stub
  SceSysmem_stub
  SceProcessmgr_stub
  SceKernelThreadMgr_stub
  SceLibKernel_stub
  SceIofilemgr_stub
)

set_target_properties(${PROJECT_NAME}
  PROPERTIES LINK_FLAGS "-nostdlib"
)

vita_create_self(${SELF_NAME}.self ${PROJECT_NAME}
  UNSAFE
  CONFIG exports.yml
)

vita_create_vpk(${SELF_NAME}.vpk ${VITA_TITLEID} ${SELF_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)
